#include "amt/analysis/CharacterAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace amt::analysis {
namespace {

double db(const double value) {
  return 20.0 * std::log10(std::max(value, 1.0e-12));
}

double median(std::vector<double> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2U;
  return values.size() % 2U == 0U ? (values[middle - 1U] + values[middle]) * 0.5
                                  : values[middle];
}

}  // namespace

struct CharacterAnalyzer::Impl {
  int sample_rate{0};
  std::size_t channels{0U};
  std::size_t window_size{2048U};
  std::size_t window_samples{0U};
  double window_sum_squares{0.0};
  double window_peak{0.0};
  std::uint64_t total_samples{0U};
  std::uint64_t near_full_scale{0U};
  std::uint64_t plateau_samples{0U};
  std::size_t active_windows{0U};
  std::size_t clipping_windows{0U};
  bool window_clipped{false};
  std::vector<double> crest_db;
  std::vector<double> previous_samples;
  std::vector<bool> previous_near_full;

  Impl(const int rate, const std::size_t channel_count)
      : sample_rate(rate), channels(channel_count),
        previous_samples(channel_count, 0.0), previous_near_full(channel_count, false) {
    if (sample_rate <= 0 || channels == 0U) {
      throw std::invalid_argument("invalid character analyzer configuration");
    }
  }

  void close_window() {
    if (window_samples == 0U) return;
    const double rms = std::sqrt(window_sum_squares / static_cast<double>(window_samples));
    if (rms > 1.0e-5) {
      ++active_windows;
      crest_db.push_back(db(window_peak) - db(rms));
      if (window_clipped) ++clipping_windows;
    }
    window_samples = 0U;
    window_sum_squares = 0.0;
    window_peak = 0.0;
    window_clipped = false;
  }
};

CharacterAnalyzer::CharacterAnalyzer(const int sample_rate, const std::size_t channels)
    : impl_(std::make_unique<Impl>(sample_rate, channels)) {}
CharacterAnalyzer::~CharacterAnalyzer() = default;
CharacterAnalyzer::CharacterAnalyzer(CharacterAnalyzer&&) noexcept = default;
CharacterAnalyzer& CharacterAnalyzer::operator=(CharacterAnalyzer&&) noexcept = default;

void CharacterAnalyzer::process(const amt::audio::AudioBuffer& buffer) {
  if (buffer.channels() != impl_->channels) {
    throw std::invalid_argument("character analyzer channel mismatch");
  }
  for (std::size_t frame = 0U; frame < buffer.frames(); ++frame) {
    double mono_power = 0.0;
    double frame_peak = 0.0;
    for (std::size_t channel = 0U; channel < impl_->channels; ++channel) {
      const double sample = buffer.channel(channel)[frame];
      const double magnitude = std::abs(sample);
      mono_power += sample * sample;
      frame_peak = std::max(frame_peak, magnitude);
      const bool near_full = magnitude >= 0.985;
      if (near_full) {
        ++impl_->near_full_scale;
        if (impl_->previous_near_full[channel] &&
            std::abs(magnitude - std::abs(impl_->previous_samples[channel])) <= 2.0e-5) {
          ++impl_->plateau_samples;
        }
      }
      if (magnitude >= 0.9999) impl_->window_clipped = true;
      impl_->previous_samples[channel] = sample;
      impl_->previous_near_full[channel] = near_full;
      ++impl_->total_samples;
    }
    impl_->window_sum_squares += mono_power / static_cast<double>(impl_->channels);
    impl_->window_peak = std::max(impl_->window_peak, frame_peak);
    ++impl_->window_samples;
    if (impl_->window_samples >= impl_->window_size) impl_->close_window();
  }
}

CharacterMetrics CharacterAnalyzer::finalize(const IntegrityMetrics& integrity) {
  impl_->close_window();
  CharacterMetrics result;
  result.near_full_scale_samples = impl_->near_full_scale;
  result.plateau_samples = impl_->plateau_samples;
  result.active_windows = impl_->active_windows;
  result.clipping_windows = impl_->clipping_windows;
  result.high_level_sample_fraction = impl_->total_samples > 0U
      ? static_cast<double>(impl_->near_full_scale) / static_cast<double>(impl_->total_samples) : 0.0;
  result.clipping_window_fraction = impl_->active_windows > 0U
      ? static_cast<double>(impl_->clipping_windows) / static_cast<double>(impl_->active_windows) : 0.0;
  result.median_window_crest_db = median(impl_->crest_db);

  const double plateau_fraction = impl_->near_full_scale > 0U
      ? static_cast<double>(impl_->plateau_samples) / static_cast<double>(impl_->near_full_scale) : 0.0;
  const double explicit_clip_fraction = impl_->total_samples > 0U
      ? static_cast<double>(integrity.clipped_samples) / static_cast<double>(impl_->total_samples) : 0.0;
  result.hard_clip_likelihood = std::clamp(
      explicit_clip_fraction * 180.0 + plateau_fraction * 0.65 +
      result.clipping_window_fraction * 0.30, 0.0, 1.0);

  const double dense_level = std::clamp(result.high_level_sample_fraction * 24.0, 0.0, 1.0);
  const double low_crest = std::clamp((9.0 - result.median_window_crest_db) / 5.0, 0.0, 1.0);
  result.saturation_likelihood = std::clamp(
      dense_level * 0.50 + low_crest * 0.35 + result.clipping_window_fraction * 0.15 -
      result.hard_clip_likelihood * 0.20, 0.0, 1.0);

  const double distributed_character = std::clamp(
      result.clipping_window_fraction / 0.35, 0.0, 1.0);
  result.intentional_character_likelihood = std::clamp(
      distributed_character * 0.45 + result.saturation_likelihood * 0.45 +
      (1.0 - plateau_fraction) * 0.10, 0.0, 1.0);

  const double isolated_clip = result.clipping_window_fraction > 0.0
      ? std::clamp((0.14 - result.clipping_window_fraction) / 0.14, 0.0, 1.0) : 0.0;
  const double long_flat_run = std::clamp(
      static_cast<double>(integrity.longest_full_scale_run) / 32.0, 0.0, 1.0);
  result.accidental_defect_risk = std::clamp(
      result.hard_clip_likelihood * (0.55 + isolated_clip * 0.30) + long_flat_run * 0.35 -
      result.intentional_character_likelihood * 0.20, 0.0, 1.0);

  const double analyzed_seconds = static_cast<double>(impl_->total_samples) /
      static_cast<double>(std::max<std::size_t>(1U, impl_->channels) *
                          static_cast<std::size_t>(impl_->sample_rate));
  result.inference_confidence = std::clamp(analyzed_seconds / 15.0, 0.2, 1.0);
  return result;
}

}  // namespace amt::analysis
