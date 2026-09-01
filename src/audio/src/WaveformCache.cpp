#include "amt/audio/WaveformCache.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace amt::audio {
namespace {

WaveformPeakBin combine(
    const std::vector<WaveformPeakBin>& source, const std::size_t begin,
    const std::size_t end) {
  WaveformPeakBin result;
  result.minimum = std::numeric_limits<float>::infinity();
  result.maximum = -std::numeric_limits<float>::infinity();
  double weighted_squares = 0.0;
  std::uint64_t frame_count = 0;
  for (std::size_t index = begin; index < end; ++index) {
    const auto& bin = source[index];
    result.minimum = std::min(result.minimum, bin.minimum);
    result.maximum = std::max(result.maximum, bin.maximum);
    weighted_squares += static_cast<double>(bin.rms) * static_cast<double>(bin.rms) *
                        static_cast<double>(bin.frames);
    frame_count += bin.frames;
  }
  result.frames = static_cast<std::uint32_t>(frame_count);
  result.rms = frame_count == 0U
                   ? 0.0F
                   : static_cast<float>(std::sqrt(weighted_squares / static_cast<double>(frame_count)));
  if (frame_count == 0U) result.minimum = result.maximum = 0.0F;
  return result;
}

}  // namespace

WaveformPeakAccumulator::WaveformPeakAccumulator(
    const int sample_rate, const std::size_t channels,
    const std::uint64_t base_frames_per_bin, const std::uint32_t level_factor)
    : sample_rate_(sample_rate), channels_(channels), base_frames_per_bin_(base_frames_per_bin),
      level_factor_(level_factor), pending_min_(channels, std::numeric_limits<float>::infinity()),
      pending_max_(channels, -std::numeric_limits<float>::infinity()),
      pending_sum_squares_(channels, 0.0), base_bins_(channels) {
  if (sample_rate_ <= 0 || channels_ == 0U || base_frames_per_bin_ == 0U || level_factor_ < 2U) {
    throw std::invalid_argument("invalid waveform cache configuration");
  }
}

void WaveformPeakAccumulator::append(const AudioBuffer& buffer) {
  if (buffer.channels() != channels_) throw std::invalid_argument("waveform channel-count mismatch");
  for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
    for (std::size_t channel_index = 0; channel_index < channels_; ++channel_index) {
      const float value = buffer.channel(channel_index)[frame];
      pending_min_[channel_index] = std::min(pending_min_[channel_index], value);
      pending_max_[channel_index] = std::max(pending_max_[channel_index], value);
      pending_sum_squares_[channel_index] += static_cast<double>(value) * value;
    }
    ++pending_frames_;
    ++source_frames_;
    if (pending_frames_ == base_frames_per_bin_) flush_base_bin();
  }
}

void WaveformPeakAccumulator::flush_base_bin() {
  if (pending_frames_ == 0U) return;
  for (std::size_t channel_index = 0; channel_index < channels_; ++channel_index) {
    base_bins_[channel_index].push_back(
        {.minimum = pending_min_[channel_index],
         .maximum = pending_max_[channel_index],
         .rms = static_cast<float>(std::sqrt(
             pending_sum_squares_[channel_index] / static_cast<double>(pending_frames_))),
         .frames = static_cast<std::uint32_t>(pending_frames_)});
    pending_min_[channel_index] = std::numeric_limits<float>::infinity();
    pending_max_[channel_index] = -std::numeric_limits<float>::infinity();
    pending_sum_squares_[channel_index] = 0.0;
  }
  pending_frames_ = 0U;
}

WaveformPeakCache WaveformPeakAccumulator::finalize() {
  flush_base_bin();
  WaveformPeakCache cache{
      .sample_rate = sample_rate_, .source_frames = source_frames_, .levels = {}};
  if (base_bins_.empty() || base_bins_.front().empty()) return cache;

  WaveformLevel current{.frames_per_bin = base_frames_per_bin_, .channels = base_bins_};
  cache.levels.push_back(current);
  while (current.channels.front().size() > 1U) {
    WaveformLevel next;
    next.frames_per_bin = current.frames_per_bin * level_factor_;
    next.channels.resize(channels_);
    for (std::size_t channel_index = 0; channel_index < channels_; ++channel_index) {
      const auto& source = current.channels[channel_index];
      for (std::size_t begin = 0; begin < source.size(); begin += level_factor_) {
        const auto end = std::min(source.size(), begin + static_cast<std::size_t>(level_factor_));
        next.channels[channel_index].push_back(combine(source, begin, end));
      }
    }
    cache.levels.push_back(next);
    current = std::move(next);
  }
  return cache;
}

}  // namespace amt::audio
