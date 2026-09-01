#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "amt/codec/AudioIO.h"
#include "amt/playback/AudioDevice.h"

namespace amt::playback {

enum class TransportState { empty, stopped, playing, paused, finished };

class Transport {
 public:
  explicit Transport(
      amt::codec::ICodecService& codecs,
      std::unique_ptr<IAudioOutputDevice> device = make_default_audio_output_device());
  ~Transport();
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  bool load(const std::filesystem::path& path, std::string& error);
  bool play(std::string& error);
  bool pause(std::string& error);
  bool resume(std::string& error);
  bool seek(std::int64_t frame, std::string& error);
  void stop() noexcept;
  void wait_until_finished() const;

  [[nodiscard]] TransportState state() const noexcept {
    return state_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::int64_t playhead_frame() const noexcept {
    return playhead_frame_.load(std::memory_order_acquire);
  }
  [[nodiscard]] const amt::codec::AudioMetadata* metadata() const noexcept;
  [[nodiscard]] std::string output_backend_name() const;

 private:
  std::size_t render(amt::audio::AudioBuffer& output, std::size_t requested_frames);
  bool start_device(std::string& error);

  amt::codec::ICodecService& codecs_;
  std::unique_ptr<IAudioOutputDevice> device_;
  std::unique_ptr<amt::codec::IAudioDecoder> decoder_;
  mutable std::mutex decoder_mutex_;
  std::atomic<TransportState> state_{TransportState::empty};
  std::atomic<std::int64_t> playhead_frame_{0};
};

}  // namespace amt::playback
