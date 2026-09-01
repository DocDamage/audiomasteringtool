#include "amt/playback/Transport.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace amt::playback {

Transport::Transport(amt::codec::ICodecService& codecs,
                     std::unique_ptr<IAudioOutputDevice> device)
    : codecs_(codecs), device_(std::move(device)) {}

Transport::~Transport() { stop(); }

bool Transport::load(const std::filesystem::path& path, std::string& error) {
  stop();
  auto decoder = codecs_.open_decoder(path, error);
  if (!decoder) return false;
  if (decoder->metadata().channels < 1 || decoder->metadata().channels > 2) {
    error = "Phase 1 native playback currently supports mono or stereo sources";
    return false;
  }
  {
    std::scoped_lock lock(decoder_mutex_);
    decoder_ = std::move(decoder);
  }
  playhead_frame_.store(0, std::memory_order_release);
  state_.store(TransportState::stopped, std::memory_order_release);
  return true;
}

bool Transport::start_device(std::string& error) {
  if (!decoder_) {
    error = "no audio source is loaded";
    return false;
  }
  const auto& info = decoder_->metadata();
  if (!device_->open(
          {.sample_rate = info.sample_rate,
           .channels = static_cast<std::size_t>(info.channels),
           .frames_per_buffer = 1024U,
           .queued_buffers = 4U},
          [this](amt::audio::AudioBuffer& output, const std::size_t frames) {
            return render(output, frames);
          },
          error)) {
    return false;
  }
  state_.store(TransportState::playing, std::memory_order_release);
  if (!device_->start(error)) {
    state_.store(TransportState::stopped, std::memory_order_release);
    return false;
  }
  return true;
}

bool Transport::play(std::string& error) {
  const auto current = state();
  if (current == TransportState::empty) {
    error = "no audio source is loaded";
    return false;
  }
  if (current == TransportState::playing) return true;
  if (current == TransportState::paused) return resume(error);
  if (current == TransportState::finished) {
    std::scoped_lock lock(decoder_mutex_);
    if (!decoder_->seek(0, error)) return false;
    playhead_frame_.store(0, std::memory_order_release);
  }
  return start_device(error);
}

bool Transport::pause(std::string& error) {
  if (state() != TransportState::playing) {
    error = "transport is not playing";
    return false;
  }
  if (!device_->pause(error)) return false;
  state_.store(TransportState::paused, std::memory_order_release);
  return true;
}

bool Transport::resume(std::string& error) {
  if (state() != TransportState::paused) {
    error = "transport is not paused";
    return false;
  }
  if (!device_->resume(error)) return false;
  state_.store(TransportState::playing, std::memory_order_release);
  return true;
}

bool Transport::seek(const std::int64_t frame, std::string& error) {
  if (!decoder_) {
    error = "no audio source is loaded";
    return false;
  }
  const bool was_playing = state() == TransportState::playing;
  const bool was_paused = state() == TransportState::paused;
  if (was_playing || was_paused) device_->stop();
  {
    std::scoped_lock lock(decoder_mutex_);
    if (!decoder_->seek(frame, error)) {
      state_.store(TransportState::stopped, std::memory_order_release);
      return false;
    }
  }
  playhead_frame_.store(frame, std::memory_order_release);
  state_.store(TransportState::stopped, std::memory_order_release);
  if (was_playing) return start_device(error);
  if (was_paused) state_.store(TransportState::paused, std::memory_order_release);
  return true;
}

void Transport::stop() noexcept {
  if (device_) device_->stop();
  std::scoped_lock lock(decoder_mutex_);
  if (decoder_) {
    std::string ignored;
    decoder_->seek(0, ignored);
    playhead_frame_.store(0, std::memory_order_release);
    state_.store(TransportState::stopped, std::memory_order_release);
  } else {
    state_.store(TransportState::empty, std::memory_order_release);
  }
}

void Transport::wait_until_finished() const {
  while (state() == TransportState::playing || state() == TransportState::paused) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

const amt::codec::AudioMetadata* Transport::metadata() const noexcept {
  return decoder_ ? &decoder_->metadata() : nullptr;
}

std::string Transport::output_backend_name() const {
  return device_ ? device_->backend_name() : "none";
}

std::size_t Transport::render(
    amt::audio::AudioBuffer& output, const std::size_t requested_frames) {
  if (state() != TransportState::playing || !decoder_) return 0U;
  std::size_t frames_read = 0U;
  std::string error;
  {
    std::scoped_lock lock(decoder_mutex_);
    if (!decoder_->read(output, requested_frames, frames_read, error)) {
      state_.store(TransportState::finished, std::memory_order_release);
      return 0U;
    }
    playhead_frame_.store(decoder_->tell(), std::memory_order_release);
  }
  if (frames_read == 0U) {
    state_.store(TransportState::finished, std::memory_order_release);
    return 0U;
  }
  return frames_read;
}

}  // namespace amt::playback
