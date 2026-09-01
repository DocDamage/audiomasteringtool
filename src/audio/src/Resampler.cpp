#include "amt/audio/Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace amt::audio {
namespace {

class WindowedSincResampler final : public IStreamingResampler {
 public:
  explicit WindowedSincResampler(ResamplerConfig config) : config_(config) {
    if (config_.input_sample_rate <= 0 || config_.output_sample_rate <= 0 ||
        config_.channels == 0U || config_.half_taps < 8U ||
        config_.passband_ratio <= 0.5 || config_.passband_ratio > 1.0) {
      throw std::invalid_argument("invalid resampler configuration");
    }
    buffers_.resize(config_.channels);
    ratio_ = static_cast<double>(config_.output_sample_rate) /
             static_cast<double>(config_.input_sample_rate);
    cutoff_ = 0.5 * std::min(1.0, ratio_) * config_.passband_ratio;
  }

  void reset() override {
    for (auto& buffer : buffers_) buffer.clear();
    buffer_start_ = 0;
    total_input_ = 0;
    produced_ = 0;
  }

  AudioBuffer process(const AudioBuffer& input, const bool end_of_input) override {
    if (input.channels() != config_.channels) {
      throw std::invalid_argument("resampler channel-count mismatch");
    }
    for (std::size_t channel_index = 0; channel_index < config_.channels; ++channel_index) {
      const auto source = input.channel(channel_index);
      buffers_[channel_index].insert(
          buffers_[channel_index].end(), source.begin(), source.end());
    }
    total_input_ += static_cast<std::int64_t>(input.frames());

    std::vector<std::vector<float>> output(config_.channels);
    const auto final_target = static_cast<std::int64_t>(
        std::llround(static_cast<double>(total_input_) * ratio_));

    while (can_produce(end_of_input, final_target)) {
      const double source_position = static_cast<double>(produced_) / ratio_;
      const auto center = static_cast<std::int64_t>(std::floor(source_position));
      for (std::size_t channel_index = 0; channel_index < config_.channels; ++channel_index) {
        output[channel_index].push_back(interpolate(channel_index, source_position, center));
      }
      ++produced_;
    }

    discard_consumed_history();

    const auto frame_count = output.empty() ? 0U : output.front().size();
    AudioBuffer result(config_.channels, frame_count);
    for (std::size_t channel_index = 0; channel_index < config_.channels; ++channel_index) {
      auto destination = result.channel(channel_index);
      std::copy(output[channel_index].begin(), output[channel_index].end(), destination.begin());
    }
    return result;
  }

  [[nodiscard]] std::int64_t input_frames_seen() const noexcept override { return total_input_; }
  [[nodiscard]] std::int64_t output_frames_produced() const noexcept override { return produced_; }

 private:
  [[nodiscard]] bool can_produce(const bool end_of_input, const std::int64_t final_target) const {
    if (end_of_input) return produced_ < final_target;
    const double source_position = static_cast<double>(produced_) / ratio_;
    const auto center = static_cast<std::int64_t>(std::floor(source_position));
    return center + static_cast<std::int64_t>(config_.half_taps) < total_input_;
  }

  [[nodiscard]] double window(const double distance) const {
    const double radius = static_cast<double>(config_.half_taps);
    const double x = std::abs(distance);
    if (x > radius) return 0.0;
    return 0.42 + 0.5 * std::cos(std::numbers::pi * x / radius) +
           0.08 * std::cos(2.0 * std::numbers::pi * x / radius);
  }

  [[nodiscard]] static double sinc(const double value) {
    if (std::abs(value) < 1.0e-12) return 1.0;
    const double angle = std::numbers::pi * value;
    return std::sin(angle) / angle;
  }

  [[nodiscard]] float sample(const std::size_t channel_index, const std::int64_t index) const {
    if (index < 0 || index >= total_input_) return 0.0F;
    const auto offset = index - buffer_start_;
    if (offset < 0 || offset >= static_cast<std::int64_t>(buffers_[channel_index].size())) {
      return 0.0F;
    }
    return buffers_[channel_index][static_cast<std::size_t>(offset)];
  }

  [[nodiscard]] float interpolate(
      const std::size_t channel_index, const double source_position,
      const std::int64_t center) const {
    const auto radius = static_cast<std::int64_t>(config_.half_taps);
    double weighted = 0.0;
    double weight_sum = 0.0;
    for (std::int64_t index = center - radius + 1; index <= center + radius; ++index) {
      const double distance = static_cast<double>(index) - source_position;
      const double weight = 2.0 * cutoff_ * sinc(2.0 * cutoff_ * distance) * window(distance);
      weighted += static_cast<double>(sample(channel_index, index)) * weight;
      weight_sum += weight;
    }
    return static_cast<float>(weight_sum == 0.0 ? 0.0 : weighted / weight_sum);
  }

  void discard_consumed_history() {
    const double next_source_position = static_cast<double>(produced_) / ratio_;
    const auto keep_from = static_cast<std::int64_t>(std::floor(next_source_position)) -
                           static_cast<std::int64_t>(config_.half_taps) - 2;
    const auto discard = std::max<std::int64_t>(0, keep_from - buffer_start_);
    for (auto& buffer : buffers_) {
      const auto count = std::min<std::size_t>(
          static_cast<std::size_t>(discard), buffer.size());
      for (std::size_t index = 0; index < count; ++index) buffer.pop_front();
    }
    buffer_start_ += discard;
  }

  ResamplerConfig config_;
  std::vector<std::deque<float>> buffers_;
  std::int64_t buffer_start_{0};
  std::int64_t total_input_{0};
  std::int64_t produced_{0};
  double ratio_{1.0};
  double cutoff_{0.47};
};

}  // namespace

std::unique_ptr<IStreamingResampler> make_high_quality_resampler(const ResamplerConfig& config) {
  return std::make_unique<WindowedSincResampler>(config);
}

}  // namespace amt::audio
