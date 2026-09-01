#include "amt/playback/AudioDevice.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif

namespace amt::playback {
namespace {

#ifdef _WIN32

class WaveOutAudioDevice final : public IAudioOutputDevice {
 public:
  ~WaveOutAudioDevice() override { stop(); }

  bool open(AudioOutputConfig config, AudioRenderCallback callback,
            std::string& error) override {
    stop();
    if (config.sample_rate <= 0 || config.channels == 0U || config.channels > 2U ||
        config.frames_per_buffer == 0U || config.queued_buffers < 2U) {
      error = "WinMM output requires 1-2 channels and a valid buffer configuration";
      return false;
    }
    if (!callback) {
      error = "audio output render callback is missing";
      return false;
    }
    config_ = config;
    callback_ = std::move(callback);
    return true;
  }

  bool start(std::string& error) override {
    if (!callback_) {
      error = "audio output device has not been opened";
      return false;
    }
    if (running_.load(std::memory_order_acquire)) return true;

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(config_.channels);
    format.nSamplesPerSec = static_cast<DWORD>(config_.sample_rate);
    format.wBitsPerSample = 16U;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * sizeof(std::int16_t));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HWAVEOUT handle = nullptr;
    const MMRESULT result = waveOutOpen(&handle, WAVE_MAPPER, &format, 0U, 0U, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR || handle == nullptr) {
      error = "waveOutOpen failed with code " + std::to_string(result);
      return false;
    }
    {
      std::scoped_lock lock(handle_mutex_);
      handle_ = handle;
    }
    running_.store(true, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    worker_ = std::thread([this] { worker_loop(); });
    return true;
  }

  bool pause(std::string& error) override {
    std::scoped_lock lock(handle_mutex_);
    if (handle_ == nullptr) {
      error = "audio output is not running";
      return false;
    }
    const auto result = waveOutPause(handle_);
    if (result != MMSYSERR_NOERROR) {
      error = "waveOutPause failed with code " + std::to_string(result);
      return false;
    }
    paused_.store(true, std::memory_order_release);
    return true;
  }

  bool resume(std::string& error) override {
    std::scoped_lock lock(handle_mutex_);
    if (handle_ == nullptr) {
      error = "audio output is not running";
      return false;
    }
    const auto result = waveOutRestart(handle_);
    if (result != MMSYSERR_NOERROR) {
      error = "waveOutRestart failed with code " + std::to_string(result);
      return false;
    }
    paused_.store(false, std::memory_order_release);
    return true;
  }

  void stop() noexcept override {
    running_.store(false, std::memory_order_release);
    {
      std::scoped_lock lock(handle_mutex_);
      if (handle_ != nullptr) waveOutReset(handle_);
    }
    if (worker_.joinable()) worker_.join();
    cleanup_handle();
  }

  bool running() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  std::string backend_name() const override { return "Windows WinMM waveOut"; }

 private:
  struct BufferSlot {
    std::vector<std::int16_t> samples;
    WAVEHDR header{};
    bool prepared{false};
    bool submitted{false};
  };

  bool render_into(BufferSlot& slot) {
    amt::audio::AudioBuffer block(config_.channels, config_.frames_per_buffer);
    const std::size_t rendered = callback_(block, config_.frames_per_buffer);
    if (rendered == 0U) return false;
    const std::size_t frames = std::min(rendered, config_.frames_per_buffer);
    slot.samples.assign(frames * config_.channels, 0);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < config_.channels; ++channel) {
        const float source = block.channel(channel)[frame];
        const float clipped = std::clamp(source, -1.0F, 1.0F);
        slot.samples[frame * config_.channels + channel] = static_cast<std::int16_t>(
            std::lrint(clipped * (clipped >= 0.0F ? 32767.0F : 32768.0F)));
      }
    }
    slot.header.lpData = reinterpret_cast<LPSTR>(slot.samples.data());
    slot.header.dwBufferLength = static_cast<DWORD>(slot.samples.size() * sizeof(std::int16_t));
    return true;
  }

  bool submit(BufferSlot& slot) {
    std::scoped_lock lock(handle_mutex_);
    if (handle_ == nullptr) return false;
    if (!slot.prepared) {
      if (waveOutPrepareHeader(handle_, &slot.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return false;
      slot.prepared = true;
    }
    if (waveOutWrite(handle_, &slot.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return false;
    slot.submitted = true;
    return true;
  }

  void worker_loop() {
    std::vector<BufferSlot> slots(config_.queued_buffers);
    std::size_t active = 0U;
    bool source_finished = false;
    for (auto& slot : slots) {
      if (!running_.load(std::memory_order_acquire) || !render_into(slot)) {
        source_finished = true;
        break;
      }
      if (!submit(slot)) {
        running_.store(false, std::memory_order_release);
        break;
      }
      ++active;
    }

    while (running_.load(std::memory_order_acquire) && active > 0U) {
      for (auto& slot : slots) {
        if (!slot.submitted || (slot.header.dwFlags & WHDR_DONE) == 0U) continue;
        slot.submitted = false;
        --active;
        if (!source_finished && render_into(slot)) {
          if (submit(slot)) {
            ++active;
          } else {
            running_.store(false, std::memory_order_release);
            break;
          }
        } else {
          source_finished = true;
        }
      }
      if (active > 0U) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    {
      std::scoped_lock lock(handle_mutex_);
      if (handle_ != nullptr) {
        waveOutReset(handle_);
        for (auto& slot : slots) {
          if (slot.prepared) {
            waveOutUnprepareHeader(handle_, &slot.header, sizeof(WAVEHDR));
            slot.prepared = false;
          }
        }
        waveOutClose(handle_);
        handle_ = nullptr;
      }
    }
    running_.store(false, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
  }

  void cleanup_handle() noexcept {
    std::scoped_lock lock(handle_mutex_);
    if (handle_ != nullptr) {
      waveOutReset(handle_);
      waveOutClose(handle_);
      handle_ = nullptr;
    }
  }

  AudioOutputConfig config_;
  AudioRenderCallback callback_;
  std::atomic_bool running_{false};
  std::atomic_bool paused_{false};
  mutable std::mutex handle_mutex_;
  HWAVEOUT handle_{nullptr};
  std::thread worker_;
};

#else

class UnsupportedAudioDevice final : public IAudioOutputDevice {
 public:
  bool open(AudioOutputConfig, AudioRenderCallback, std::string& error) override {
    error = "native audio output is currently implemented for Windows only";
    return false;
  }
  bool start(std::string& error) override {
    error = "native audio output is currently implemented for Windows only";
    return false;
  }
  bool pause(std::string& error) override {
    error = "native audio output is currently implemented for Windows only";
    return false;
  }
  bool resume(std::string& error) override {
    error = "native audio output is currently implemented for Windows only";
    return false;
  }
  void stop() noexcept override {}
  bool running() const noexcept override { return false; }
  std::string backend_name() const override { return "unsupported native output"; }
};

#endif

}  // namespace

std::unique_ptr<IAudioOutputDevice> make_default_audio_output_device() {
#ifdef _WIN32
  return std::make_unique<WaveOutAudioDevice>();
#else
  return std::make_unique<UnsupportedAudioDevice>();
#endif
}

}  // namespace amt::playback
