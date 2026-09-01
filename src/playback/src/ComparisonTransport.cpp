#include "amt/playback/ComparisonTransport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

namespace amt::playback {
namespace {

double db_to_linear(const double db) { return std::pow(10.0, db / 20.0); }

}  // namespace

ComparisonTransport::ComparisonTransport(amt::codec::ICodecService& codecs,
                                         std::unique_ptr<IAudioOutputDevice> device)
    : codecs_(codecs), device_(std::move(device)) {}

ComparisonTransport::~ComparisonTransport() { stop(); }

bool ComparisonTransport::load(const ComparisonItem& original, const ComparisonItem& master_a,
                               const ComparisonItem& master_b, std::string& error) {
  stop();
  const std::array<ComparisonItem, 3> items = {original, master_a, master_b};
  std::array<std::unique_ptr<amt::codec::IAudioDecoder>, 3> opened;
  for (std::size_t index = 0; index < items.size(); ++index) {
    opened[index] = codecs_.open_decoder(items[index].path, error);
    if (!opened[index]) return false;
    if (opened[index]->metadata().channels < 1 || opened[index]->metadata().channels > 2) {
      error = "comparison playback supports mono or stereo sources";
      return false;
    }
  }
  const auto& reference = opened[0]->metadata();
  for (std::size_t index = 1U; index < opened.size(); ++index) {
    const auto& info = opened[index]->metadata();
    if (info.sample_rate != reference.sample_rate || info.channels != reference.channels ||
        info.frames != reference.frames) {
      error = "comparison sources must have identical sample rate, channels, and frame count";
      return false;
    }
  }
  {
    std::scoped_lock lock(decoder_mutex_);
    decoders_ = std::move(opened);
  }
  for (std::size_t index = 0; index < items.size(); ++index) {
    gains_[index] = db_to_linear(items[index].audition_gain_db);
  }
  selected_.store(0, std::memory_order_release);
  render_source_ = 0;
  previous_source_ = 0;
  crossfade_remaining_ = 0U;
  crossfade_total_ = std::max<std::size_t>(
      1U, static_cast<std::size_t>(std::llround(reference.sample_rate * 0.012)));
  playhead_frame_.store(0, std::memory_order_release);
  state_.store(TransportState::stopped, std::memory_order_release);
  return true;
}

void ComparisonTransport::select(const ComparisonSource source) noexcept {
  selected_.store(static_cast<int>(source), std::memory_order_release);
}

bool ComparisonTransport::start_device(std::string& error) {
  if (!decoders_[0]) {
    error = "no comparison sources are loaded";
    return false;
  }
  const auto& info = decoders_[0]->metadata();
  if (!device_->open(
          {.sample_rate = info.sample_rate,
           .channels = static_cast<std::size_t>(info.channels),
           .frames_per_buffer = 1024U,
           .queued_buffers = 4U},
          [this](amt::audio::AudioBuffer& output, const std::size_t frames) {
            return render(output, frames);
          }, error)) {
    return false;
  }
  state_.store(TransportState::playing, std::memory_order_release);
  if (!device_->start(error)) {
    state_.store(TransportState::stopped, std::memory_order_release);
    return false;
  }
  return true;
}

bool ComparisonTransport::play(std::string& error) {
  const auto current = state();
  if (current == TransportState::empty) {
    error = "no comparison sources are loaded";
    return false;
  }
  if (current == TransportState::playing) return true;
  if (current == TransportState::paused) return resume(error);
  if (current == TransportState::finished) {
    std::scoped_lock lock(decoder_mutex_);
    for (auto& decoder : decoders_) {
      if (!decoder->seek(0, error)) return false;
    }
    playhead_frame_.store(0, std::memory_order_release);
  }
  return start_device(error);
}

bool ComparisonTransport::pause(std::string& error) {
  if (state() != TransportState::playing) {
    error = "comparison transport is not playing";
    return false;
  }
  if (!device_->pause(error)) return false;
  state_.store(TransportState::paused, std::memory_order_release);
  return true;
}

bool ComparisonTransport::resume(std::string& error) {
  if (state() != TransportState::paused) {
    error = "comparison transport is not paused";
    return false;
  }
  if (!device_->resume(error)) return false;
  state_.store(TransportState::playing, std::memory_order_release);
  return true;
}

bool ComparisonTransport::seek(const std::int64_t frame, std::string& error) {
  if (!decoders_[0]) {
    error = "no comparison sources are loaded";
    return false;
  }
  const bool was_playing = state() == TransportState::playing;
  const bool was_paused = state() == TransportState::paused;
  if (was_playing || was_paused) device_->stop();
  {
    std::scoped_lock lock(decoder_mutex_);
    for (auto& decoder : decoders_) {
      if (!decoder->seek(frame, error)) {
        state_.store(TransportState::stopped, std::memory_order_release);
        return false;
      }
    }
  }
  playhead_frame_.store(frame, std::memory_order_release);
  state_.store(TransportState::stopped, std::memory_order_release);
  crossfade_remaining_ = 0U;
  render_source_ = selected_.load(std::memory_order_acquire);
  previous_source_ = render_source_;
  if (was_playing) return start_device(error);
  if (was_paused) {
    if (!start_device(error)) return false;
    return pause(error);
  }
  return true;
}

void ComparisonTransport::stop() noexcept {
  if (device_) device_->stop();
  std::scoped_lock lock(decoder_mutex_);
  bool loaded = false;
  for (auto& decoder : decoders_) {
    if (decoder) {
      loaded = true;
      std::string ignored;
      decoder->seek(0, ignored);
    }
  }
  playhead_frame_.store(0, std::memory_order_release);
  state_.store(loaded ? TransportState::stopped : TransportState::empty,
               std::memory_order_release);
}

void ComparisonTransport::wait_until_finished() const {
  while (state() == TransportState::playing || state() == TransportState::paused) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

const amt::codec::AudioMetadata* ComparisonTransport::metadata() const noexcept {
  return decoders_[0] ? &decoders_[0]->metadata() : nullptr;
}

std::string ComparisonTransport::output_backend_name() const {
  return device_ ? device_->backend_name() : "none";
}

std::size_t ComparisonTransport::render(amt::audio::AudioBuffer& output,
                                        const std::size_t requested_frames) {
  if (state() != TransportState::playing || !decoders_[0]) return 0U;
  std::array<amt::audio::AudioBuffer, 3> buffers;
  std::array<std::size_t, 3> counts{};
  std::string error;
  {
    std::scoped_lock lock(decoder_mutex_);
    for (std::size_t index = 0; index < decoders_.size(); ++index) {
      if (!decoders_[index]->read(buffers[index], requested_frames, counts[index], error)) {
        state_.store(TransportState::finished, std::memory_order_release);
        return 0U;
      }
    }
    playhead_frame_.store(decoders_[0]->tell(), std::memory_order_release);
  }
  const std::size_t frames = *std::min_element(counts.begin(), counts.end());
  if (frames == 0U) {
    state_.store(TransportState::finished, std::memory_order_release);
    return 0U;
  }

  const int requested_source = std::clamp(selected_.load(std::memory_order_acquire), 0, 2);
  if (requested_source != render_source_) {
    previous_source_ = render_source_;
    render_source_ = requested_source;
    crossfade_remaining_ = crossfade_total_;
  }

  output.resize(buffers[0].channels(), frames);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    double new_weight = 1.0;
    double old_weight = 0.0;
    if (crossfade_remaining_ > 0U) {
      new_weight = 1.0 - static_cast<double>(crossfade_remaining_) /
                             static_cast<double>(crossfade_total_);
      old_weight = 1.0 - new_weight;
      --crossfade_remaining_;
    }
    for (std::size_t channel = 0; channel < output.channels(); ++channel) {
      const double current = buffers[static_cast<std::size_t>(render_source_)].channel(channel)[frame] *
                             gains_[static_cast<std::size_t>(render_source_)];
      double value = current;
      if (old_weight > 0.0) {
        const double previous = buffers[static_cast<std::size_t>(previous_source_)].channel(channel)[frame] *
                                gains_[static_cast<std::size_t>(previous_source_)];
        value = previous * old_weight + current * new_weight;
      }
      output.channel(channel)[frame] = static_cast<float>(value);
    }
  }
  if (crossfade_remaining_ == 0U) previous_source_ = render_source_;
  return frames;
}

}  // namespace amt::playback
