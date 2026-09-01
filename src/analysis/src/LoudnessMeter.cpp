#include "amt/analysis/LoudnessMeter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ebur128.h"

namespace amt::analysis {
namespace {

double to_db(const double linear) {
  return linear > 0.0 ? 20.0 * std::log10(linear) : -std::numeric_limits<double>::infinity();
}

double finite_or_floor(const double value, const double floor = -200.0) {
  return std::isfinite(value) ? value : floor;
}

double percentile(std::vector<double> values, const double fraction) {
  values.erase(std::remove_if(values.begin(), values.end(),
                              [](const double value) { return !std::isfinite(value); }),
               values.end());
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double position = fraction * static_cast<double>(values.size() - 1U);
  const auto lower = static_cast<std::size_t>(std::floor(position));
  const auto upper = static_cast<std::size_t>(std::ceil(position));
  if (lower == upper) return values[lower];
  const double blend = position - static_cast<double>(lower);
  return values[lower] * (1.0 - blend) + values[upper] * blend;
}

std::vector<int> annex1_channel_map(const std::size_t channels) {
  switch (channels) {
    case 1U:
      return {EBUR128_LEFT};
    case 2U:
      return {EBUR128_LEFT, EBUR128_RIGHT};
    case 3U:
      return {EBUR128_LEFT, EBUR128_RIGHT, EBUR128_CENTER};
    case 4U:
      return {EBUR128_LEFT, EBUR128_RIGHT, EBUR128_LEFT_SURROUND, EBUR128_RIGHT_SURROUND};
    case 5U:
      return {EBUR128_LEFT, EBUR128_RIGHT, EBUR128_CENTER,
              EBUR128_LEFT_SURROUND, EBUR128_RIGHT_SURROUND};
    case 6U:
      return {EBUR128_LEFT, EBUR128_RIGHT, EBUR128_CENTER, EBUR128_UNUSED,
              EBUR128_LEFT_SURROUND, EBUR128_RIGHT_SURROUND};
    default:
      return {};
  }
}

}  // namespace

struct LoudnessMeter::Impl {
  int sample_rate{0};
  std::size_t channels{0};
  ebur128_state* state{nullptr};
  std::int64_t frames{0};
  double sum_squares{0.0};
  std::uint64_t finite_samples{0};
  std::vector<LoudnessPoint> timeline;
  double max_momentary{-std::numeric_limits<double>::infinity()};
  double max_short_term{-std::numeric_limits<double>::infinity()};

  Impl(const int rate, const std::size_t channel_count)
      : sample_rate(rate), channels(channel_count) {
    const auto channel_map = annex1_channel_map(channels);
    if (sample_rate <= 0 || channel_map.empty()) {
      throw std::invalid_argument("BS.1770 Annex 1 meter supports conventional 1-6 channel layouts");
    }
    const int mode = EBUR128_MODE_I | EBUR128_MODE_LRA | EBUR128_MODE_TRUE_PEAK;
    state = ebur128_init(static_cast<unsigned int>(channels),
                         static_cast<unsigned long>(sample_rate), mode);
    if (state == nullptr) throw std::runtime_error("failed to initialize loudness meter");
    for (std::size_t channel = 0; channel < channel_map.size(); ++channel) {
      if (ebur128_set_channel(state, static_cast<unsigned int>(channel), channel_map[channel]) !=
          EBUR128_SUCCESS) {
        ebur128_destroy(&state);
        throw std::runtime_error("failed to configure BS.1770 channel map");
      }
    }
  }

  ~Impl() {
    if (state != nullptr) ebur128_destroy(&state);
  }
};

LoudnessMeter::LoudnessMeter(const int sample_rate, const std::size_t channels)
    : impl_(std::make_unique<Impl>(sample_rate, channels)) {}
LoudnessMeter::~LoudnessMeter() = default;
LoudnessMeter::LoudnessMeter(LoudnessMeter&&) noexcept = default;
LoudnessMeter& LoudnessMeter::operator=(LoudnessMeter&&) noexcept = default;

void LoudnessMeter::process(const amt::audio::AudioBuffer& buffer) {
  if (buffer.channels() != impl_->channels) throw std::invalid_argument("loudness channel mismatch");
  if (buffer.empty()) return;

  std::vector<float> interleaved;
  buffer.to_interleaved(interleaved);
  if (ebur128_add_frames_float(impl_->state, interleaved.data(), buffer.frames()) != EBUR128_SUCCESS) {
    throw std::runtime_error("loudness meter rejected audio frames");
  }
  for (const float sample : interleaved) {
    if (std::isfinite(sample)) {
      impl_->sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
      ++impl_->finite_samples;
    }
  }
  impl_->frames += static_cast<std::int64_t>(buffer.frames());

  double momentary = -std::numeric_limits<double>::infinity();
  double short_term = -std::numeric_limits<double>::infinity();
  ebur128_loudness_momentary(impl_->state, &momentary);
  ebur128_loudness_shortterm(impl_->state, &short_term);
  impl_->max_momentary = std::max(impl_->max_momentary, momentary);
  impl_->max_short_term = std::max(impl_->max_short_term, short_term);
  impl_->timeline.push_back(
      {.time_seconds = static_cast<double>(impl_->frames) / static_cast<double>(impl_->sample_rate),
       .momentary_lufs = finite_or_floor(momentary),
       .short_term_lufs = finite_or_floor(short_term)});
}

LoudnessMetrics LoudnessMeter::finalize() {
  LoudnessMetrics result;
  double integrated = -std::numeric_limits<double>::infinity();
  double range = 0.0;
  if (ebur128_loudness_global(impl_->state, &integrated) != EBUR128_SUCCESS) {
    throw std::runtime_error("integrated loudness calculation failed");
  }
  if (ebur128_loudness_range(impl_->state, &range) != EBUR128_SUCCESS) range = 0.0;

  double sample_peak = 0.0;
  double true_peak = 0.0;
  for (std::size_t channel = 0; channel < impl_->channels; ++channel) {
    double channel_sample_peak = 0.0;
    double channel_true_peak = 0.0;
    if (ebur128_sample_peak(impl_->state, static_cast<unsigned int>(channel),
                            &channel_sample_peak) == EBUR128_SUCCESS) {
      sample_peak = std::max(sample_peak, channel_sample_peak);
    }
    if (ebur128_true_peak(impl_->state, static_cast<unsigned int>(channel),
                          &channel_true_peak) == EBUR128_SUCCESS) {
      true_peak = std::max(true_peak, channel_true_peak);
    }
  }

  const double rms = impl_->finite_samples == 0U
                         ? 0.0
                         : std::sqrt(impl_->sum_squares /
                                     static_cast<double>(impl_->finite_samples));
  const double sample_peak_db = to_db(sample_peak);
  const double true_peak_db = to_db(true_peak);
  const double rms_db = to_db(rms);
  std::vector<double> short_term_values;
  short_term_values.reserve(impl_->timeline.size());
  for (const auto& point : impl_->timeline) {
    if (point.short_term_lufs > -199.0) short_term_values.push_back(point.short_term_lufs);
  }

  result.integrated_lufs = finite_or_floor(integrated);
  result.loudness_range_lu = range;
  result.max_momentary_lufs = finite_or_floor(impl_->max_momentary);
  result.max_short_term_lufs = finite_or_floor(impl_->max_short_term);
  result.short_term_variation_lu = short_term_values.empty()
                                       ? 0.0
                                       : percentile(short_term_values, 0.95) -
                                             percentile(short_term_values, 0.10);
  result.sample_peak_dbfs = finite_or_floor(sample_peak_db);
  result.true_peak_dbtp = finite_or_floor(true_peak_db);
  result.crest_factor_db = std::isfinite(sample_peak_db) && std::isfinite(rms_db)
                               ? sample_peak_db - rms_db
                               : 0.0;
  result.peak_to_loudness_ratio_db =
      std::isfinite(integrated) && std::isfinite(true_peak_db) ? true_peak_db - integrated : 0.0;
  result.timeline = impl_->timeline;
  return result;
}

}  // namespace amt::analysis
