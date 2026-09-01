#include "amt/audio/AudioBuffer.h"

#include <algorithm>
#include <stdexcept>

namespace amt::audio {

AudioBuffer::AudioBuffer(const std::size_t channels, const std::size_t frames) {
  resize(channels, frames);
}

void AudioBuffer::resize(const std::size_t channels, const std::size_t frames) {
  channels_.assign(channels, std::vector<float>(frames, 0.0F));
}

void AudioBuffer::resize_frames(const std::size_t frames) {
  for (auto& channel_data : channels_) channel_data.resize(frames, 0.0F);
}

void AudioBuffer::clear() noexcept {
  for (auto& channel_data : channels_) {
    std::fill(channel_data.begin(), channel_data.end(), 0.0F);
  }
}

std::span<float> AudioBuffer::channel(const std::size_t index) {
  if (index >= channels_.size()) throw std::out_of_range("audio channel index");
  return channels_[index];
}

std::span<const float> AudioBuffer::channel(const std::size_t index) const {
  if (index >= channels_.size()) throw std::out_of_range("audio channel index");
  return channels_[index];
}

void AudioBuffer::to_interleaved(std::vector<float>& output) const {
  const auto frame_count = frames();
  output.assign(frame_count * channels(), 0.0F);
  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    for (std::size_t channel_index = 0; channel_index < channels(); ++channel_index) {
      output[frame * channels() + channel_index] = channels_[channel_index][frame];
    }
  }
}

AudioBuffer AudioBuffer::from_interleaved(
    const std::span<const float> input, const std::size_t channel_count) {
  if (channel_count == 0U || input.size() % channel_count != 0U) {
    throw std::invalid_argument("invalid interleaved audio shape");
  }
  const auto frame_count = input.size() / channel_count;
  AudioBuffer result(channel_count, frame_count);
  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    for (std::size_t channel_index = 0; channel_index < channel_count; ++channel_index) {
      result.channels_[channel_index][frame] = input[frame * channel_count + channel_index];
    }
  }
  return result;
}

}  // namespace amt::audio
