#include "amt/analysis/IntegrityAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace amt::analysis {

struct IntegrityAnalyzer::Impl {
  int sample_rate{0};
  std::size_t channels{0};
  std::uint64_t frames{0};
  std::uint64_t nan_samples{0};
  std::uint64_t infinite_samples{0};
  std::uint64_t clipped_samples{0};
  std::uint64_t repeated_full_scale_runs{0};
  std::uint32_t longest_full_scale_run{0};
  std::vector<double> sums;
  std::vector<double> sums_squares;
  std::vector<std::uint64_t> finite_counts;
  std::vector<std::uint32_t> current_full_scale_run;
  std::int64_t first_non_silent{-1};
  std::int64_t last_non_silent{-1};

  Impl(const int rate, const std::size_t channel_count)
      : sample_rate(rate), channels(channel_count), sums(channel_count, 0.0),
        sums_squares(channel_count, 0.0), finite_counts(channel_count, 0U),
        current_full_scale_run(channel_count, 0U) {
    if (sample_rate <= 0 || channels == 0U) throw std::invalid_argument("invalid integrity analyzer");
  }
};

IntegrityAnalyzer::IntegrityAnalyzer(const int sample_rate, const std::size_t channels)
    : impl_(std::make_unique<Impl>(sample_rate, channels)) {}
IntegrityAnalyzer::~IntegrityAnalyzer() = default;
IntegrityAnalyzer::IntegrityAnalyzer(IntegrityAnalyzer&&) noexcept = default;
IntegrityAnalyzer& IntegrityAnalyzer::operator=(IntegrityAnalyzer&&) noexcept = default;

void IntegrityAnalyzer::process(const amt::audio::AudioBuffer& buffer) {
  if (buffer.channels() != impl_->channels) throw std::invalid_argument("integrity channel mismatch");
  constexpr double silence_threshold = 0.00003162277660168379;  // -90 dBFS
  for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
    bool frame_non_silent = false;
    for (std::size_t channel = 0; channel < impl_->channels; ++channel) {
      const float raw = buffer.channel(channel)[frame];
      if (std::isnan(raw)) {
        ++impl_->nan_samples;
        impl_->current_full_scale_run[channel] = 0U;
        continue;
      }
      if (!std::isfinite(raw)) {
        ++impl_->infinite_samples;
        impl_->current_full_scale_run[channel] = 0U;
        continue;
      }
      const double value = raw;
      const double absolute = std::abs(value);
      impl_->sums[channel] += value;
      impl_->sums_squares[channel] += value * value;
      ++impl_->finite_counts[channel];
      if (absolute >= 1.0) ++impl_->clipped_samples;
      if (absolute >= 0.999969482421875) {
        ++impl_->current_full_scale_run[channel];
        impl_->longest_full_scale_run =
            std::max(impl_->longest_full_scale_run, impl_->current_full_scale_run[channel]);
        if (impl_->current_full_scale_run[channel] == 3U) ++impl_->repeated_full_scale_runs;
      } else {
        impl_->current_full_scale_run[channel] = 0U;
      }
      frame_non_silent = frame_non_silent || absolute > silence_threshold;
    }
    const auto absolute_frame = static_cast<std::int64_t>(impl_->frames + frame);
    if (frame_non_silent) {
      if (impl_->first_non_silent < 0) impl_->first_non_silent = absolute_frame;
      impl_->last_non_silent = absolute_frame;
    }
  }
  impl_->frames += buffer.frames();
}

IntegrityMetrics IntegrityAnalyzer::finalize() const {
  IntegrityMetrics result;
  result.nan_samples = impl_->nan_samples;
  result.infinite_samples = impl_->infinite_samples;
  result.clipped_samples = impl_->clipped_samples;
  result.repeated_full_scale_runs = impl_->repeated_full_scale_runs;
  result.longest_full_scale_run = impl_->longest_full_scale_run;

  std::vector<double> channel_rms;
  for (std::size_t channel = 0; channel < impl_->channels; ++channel) {
    if (impl_->finite_counts[channel] == 0U) continue;
    const double count = static_cast<double>(impl_->finite_counts[channel]);
    result.max_absolute_dc_offset =
        std::max(result.max_absolute_dc_offset, std::abs(impl_->sums[channel] / count));
    channel_rms.push_back(std::sqrt(impl_->sums_squares[channel] / count));
  }
  if (channel_rms.size() >= 2U) {
    const auto [minimum, maximum] = std::minmax_element(channel_rms.begin(), channel_rms.end());
    if (*minimum > 0.0 && *maximum > 0.0) {
      result.channel_imbalance_db = 20.0 * std::log10(*maximum / *minimum);
    }
  }

  const double rate = static_cast<double>(impl_->sample_rate);
  if (impl_->first_non_silent < 0) {
    result.head_silence_seconds = static_cast<double>(impl_->frames) / rate;
    result.tail_silence_seconds = result.head_silence_seconds;
  } else {
    result.head_silence_seconds = static_cast<double>(impl_->first_non_silent) / rate;
    result.tail_silence_seconds = static_cast<double>(
        static_cast<std::int64_t>(impl_->frames) - impl_->last_non_silent - 1) / rate;
  }
  return result;
}

}  // namespace amt::analysis
