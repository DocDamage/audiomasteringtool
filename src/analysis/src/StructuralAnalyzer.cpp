#include "amt/analysis/StructuralAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace amt::analysis {
namespace {

constexpr double kFloorDb = -120.0;

double to_db(const double value) {
  return value > 1.0e-12 ? 20.0 * std::log10(value) : kFloorDb;
}

double percentile(std::vector<double> values, const double fraction) {
  if (values.empty()) return kFloorDb;
  std::sort(values.begin(), values.end());
  const double position = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(values.size() - 1U);
  const auto lower = static_cast<std::size_t>(std::floor(position));
  const auto upper = static_cast<std::size_t>(std::ceil(position));
  const double mix = position - static_cast<double>(lower);
  return values[lower] + (values[upper] - values[lower]) * mix;
}

struct FeatureFrame {
  double time_seconds{0.0};
  double rms{0.0};
  double rms_db{kFloorDb};
  double peak{0.0};
  double brightness{0.0};
  double width{0.0};
  double onset{0.0};
};

struct SectionWindow {
  std::size_t first_frame{0U};
  std::size_t last_frame{0U};
  double start_seconds{0.0};
  double end_seconds{0.0};
  double energy_db{kFloorDb};
  double brightness{0.0};
  double transient_density{0.0};
  double width{0.0};
};

}  // namespace

struct StructuralAnalyzer::Impl {
  int sample_rate{0};
  std::size_t channels{0U};
  std::size_t frame_size{1024U};
  std::size_t pending_samples{0U};
  std::int64_t total_samples{0};
  double sum_squares{0.0};
  double sum_abs{0.0};
  double sum_abs_diff{0.0};
  double mid_squares{0.0};
  double side_squares{0.0};
  double peak{0.0};
  double previous_mono{0.0};
  double previous_rms{0.0};
  std::vector<FeatureFrame> frames;

  Impl(const int rate, const std::size_t channel_count)
      : sample_rate(rate), channels(channel_count) {
    if (sample_rate <= 0 || channels == 0U) {
      throw std::invalid_argument("invalid structural analyzer configuration");
    }
  }

  void close_frame() {
    if (pending_samples == 0U) return;
    const double count = static_cast<double>(pending_samples);
    const double rms = std::sqrt(sum_squares / count);
    const double brightness = std::clamp(sum_abs_diff / std::max(2.0 * sum_abs, 1.0e-9), 0.0, 1.0);
    const double width = channels >= 2U
        ? std::clamp(std::sqrt(side_squares / std::max(mid_squares + side_squares, 1.0e-12)), 0.0, 1.0)
        : 0.0;
    const double rms_jump = std::max(0.0, to_db(rms) - to_db(previous_rms));
    const double peak_jump = std::max(0.0, peak - previous_rms) / std::max(previous_rms, 1.0e-5);
    const double onset = std::clamp(rms_jump / 10.0 + peak_jump * 0.15, 0.0, 1.5);
    const double center_sample = static_cast<double>(total_samples) - count * 0.5;
    frames.push_back({.time_seconds = center_sample / static_cast<double>(sample_rate),
                      .rms = rms,
                      .rms_db = to_db(rms),
                      .peak = peak,
                      .brightness = brightness,
                      .width = width,
                      .onset = onset});
    previous_rms = rms;
    pending_samples = 0U;
    sum_squares = 0.0;
    sum_abs = 0.0;
    sum_abs_diff = 0.0;
    mid_squares = 0.0;
    side_squares = 0.0;
    peak = 0.0;
  }
};

StructuralAnalyzer::StructuralAnalyzer(const int sample_rate, const std::size_t channels)
    : impl_(std::make_unique<Impl>(sample_rate, channels)) {}
StructuralAnalyzer::~StructuralAnalyzer() = default;
StructuralAnalyzer::StructuralAnalyzer(StructuralAnalyzer&&) noexcept = default;
StructuralAnalyzer& StructuralAnalyzer::operator=(StructuralAnalyzer&&) noexcept = default;

void StructuralAnalyzer::process(const amt::audio::AudioBuffer& buffer) {
  if (buffer.channels() != impl_->channels) {
    throw std::invalid_argument("structural analyzer channel mismatch");
  }
  for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
    double mono = 0.0;
    for (std::size_t channel = 0; channel < impl_->channels; ++channel) {
      mono += static_cast<double>(buffer.channel(channel)[frame]);
    }
    mono /= static_cast<double>(impl_->channels);
    impl_->sum_squares += mono * mono;
    impl_->sum_abs += std::abs(mono);
    impl_->sum_abs_diff += std::abs(mono - impl_->previous_mono);
    impl_->previous_mono = mono;
    impl_->peak = std::max(impl_->peak, std::abs(mono));
    if (impl_->channels >= 2U) {
      const double left = buffer.channel(0U)[frame];
      const double right = buffer.channel(1U)[frame];
      const double mid = (left + right) * 0.5;
      const double side = (left - right) * 0.5;
      impl_->mid_squares += mid * mid;
      impl_->side_squares += side * side;
    }
    ++impl_->pending_samples;
    ++impl_->total_samples;
    if (impl_->pending_samples >= impl_->frame_size) impl_->close_frame();
  }
}

StructuralMetrics StructuralAnalyzer::finalize() {
  impl_->close_frame();
  StructuralMetrics result;
  if (impl_->frames.empty()) return result;

  const double feature_rate = static_cast<double>(impl_->sample_rate) /
                              static_cast<double>(impl_->frame_size);
  std::vector<double> onset;
  std::vector<double> rms_db;
  std::vector<double> crest;
  onset.reserve(impl_->frames.size());
  rms_db.reserve(impl_->frames.size());
  crest.reserve(impl_->frames.size());
  for (const auto& frame : impl_->frames) {
    onset.push_back(frame.onset);
    rms_db.push_back(frame.rms_db);
    crest.push_back(to_db(frame.peak) - frame.rms_db);
  }

  const double onset_threshold = percentile(onset, 0.75) + 0.03;
  std::size_t onset_count = 0U;
  double onset_sum = 0.0;
  for (std::size_t index = 1U; index + 1U < onset.size(); ++index) {
    if (onset[index] > onset_threshold && onset[index] >= onset[index - 1U] &&
        onset[index] >= onset[index + 1U]) {
      ++onset_count;
      onset_sum += onset[index];
    }
  }
  const double duration_seconds = static_cast<double>(impl_->total_samples) /
                                  static_cast<double>(impl_->sample_rate);
  result.tempo.onset_density_per_second = duration_seconds > 0.0
      ? static_cast<double>(onset_count) / duration_seconds : 0.0;
  result.tempo.mean_onset_strength = onset_count > 0U ? onset_sum / static_cast<double>(onset_count) : 0.0;
  result.tempo.transient_fraction = onset.empty() ? 0.0
      : static_cast<double>(std::count_if(onset.begin(), onset.end(),
          [onset_threshold](const double value) { return value > onset_threshold; })) /
        static_cast<double>(onset.size());

  const int min_lag = std::max(1, static_cast<int>(std::lround(feature_rate * 60.0 / 180.0)));
  const int max_lag = std::max(min_lag + 1,
      static_cast<int>(std::lround(feature_rate * 60.0 / 60.0)));
  double best_score = 0.0;
  int best_lag = 0;
  for (int lag = min_lag; lag <= max_lag; ++lag) {
    if (static_cast<std::size_t>(lag) >= onset.size()) break;
    double cross = 0.0;
    double left_energy = 0.0;
    double right_energy = 0.0;
    for (std::size_t index = static_cast<std::size_t>(lag); index < onset.size(); ++index) {
      const double left = std::max(0.0, onset[index]);
      const double right = std::max(0.0, onset[index - static_cast<std::size_t>(lag)]);
      cross += left * right;
      left_energy += left * left;
      right_energy += right * right;
    }
    const double normalized = cross / std::sqrt(std::max(left_energy * right_energy, 1.0e-12));
    const double speed_preference = 1.0 + 0.08 *
        (1.0 - static_cast<double>(lag - min_lag) / static_cast<double>(std::max(1, max_lag - min_lag)));
    const double score = normalized * speed_preference;
    if (score > best_score) {
      best_score = score;
      best_lag = lag;
    }
  }
  if (best_lag > 0 && onset_count >= 3U) {
    result.tempo.bpm = 60.0 * feature_rate / static_cast<double>(best_lag);
    result.tempo.confidence = std::clamp(best_score, 0.0, 1.0) *
        std::clamp(static_cast<double>(onset_count) / 16.0, 0.25, 1.0);
  }

  result.macro_dynamics.rms_p10_dbfs = percentile(rms_db, 0.10);
  result.macro_dynamics.rms_p50_dbfs = percentile(rms_db, 0.50);
  result.macro_dynamics.rms_p90_dbfs = percentile(rms_db, 0.90);
  result.macro_dynamics.macro_dynamic_range_db =
      std::max(0.0, result.macro_dynamics.rms_p90_dbfs - result.macro_dynamics.rms_p10_dbfs);
  result.macro_dynamics.crest_variability_db =
      std::max(0.0, percentile(crest, 0.90) - percentile(crest, 0.10));

  const std::size_t section_window_frames = std::max<std::size_t>(
      1U, static_cast<std::size_t>(std::lround(feature_rate * 2.0)));
  std::vector<SectionWindow> windows;
  for (std::size_t start = 0U; start < impl_->frames.size(); start += section_window_frames) {
    const std::size_t end = std::min(impl_->frames.size(), start + section_window_frames);
    double power = 0.0;
    double brightness = 0.0;
    double width = 0.0;
    std::size_t transients = 0U;
    for (std::size_t index = start; index < end; ++index) {
      power += impl_->frames[index].rms * impl_->frames[index].rms;
      brightness += impl_->frames[index].brightness;
      width += impl_->frames[index].width;
      if (impl_->frames[index].onset > onset_threshold) ++transients;
    }
    const double count = static_cast<double>(end - start);
    const double rms = std::sqrt(power / std::max(count, 1.0));
    const double start_seconds = static_cast<double>(start * impl_->frame_size) /
                                 static_cast<double>(impl_->sample_rate);
    const double end_seconds = std::min(duration_seconds,
        static_cast<double>(end * impl_->frame_size) / static_cast<double>(impl_->sample_rate));
    windows.push_back({.first_frame = start,
                       .last_frame = end,
                       .start_seconds = start_seconds,
                       .end_seconds = end_seconds,
                       .energy_db = to_db(rms),
                       .brightness = brightness / std::max(count, 1.0),
                       .transient_density = static_cast<double>(transients) / std::max(count, 1.0),
                       .width = width / std::max(count, 1.0)});
  }

  std::vector<double> distances(windows.size(), 0.0);
  for (std::size_t index = 1U; index < windows.size(); ++index) {
    const auto& a = windows[index - 1U];
    const auto& b = windows[index];
    const double energy = std::abs(a.energy_db - b.energy_db) / 5.0;
    const double bright = std::abs(a.brightness - b.brightness) / 0.10;
    const double trans = std::abs(a.transient_density - b.transient_density) / 0.15;
    const double width = std::abs(a.width - b.width) / 0.15;
    distances[index] = std::sqrt(energy * energy + bright * bright + trans * trans + width * width);
  }
  std::vector<double> nonzero_distances;
  if (distances.size() > 1U) nonzero_distances.assign(distances.begin() + 1, distances.end());
  const double boundary_threshold = std::max(1.1, percentile(nonzero_distances, 0.70) * 1.15);
  const std::size_t min_segment_windows = 2U;
  std::vector<std::size_t> boundaries{0U};
  std::size_t last_boundary = 0U;
  for (std::size_t index = 1U; index + 1U < windows.size(); ++index) {
    if (index - last_boundary < min_segment_windows) continue;
    if (distances[index] >= boundary_threshold && distances[index] >= distances[index - 1U] &&
        distances[index] >= distances[index + 1U]) {
      boundaries.push_back(index);
      last_boundary = index;
    }
  }
  boundaries.push_back(windows.size());
  result.boundary_count = boundaries.size() >= 2U ? boundaries.size() - 2U : 0U;

  std::vector<double> window_energy;
  window_energy.reserve(windows.size());
  for (const auto& window : windows) window_energy.push_back(window.energy_db);
  const double median_energy = percentile(window_energy, 0.50);
  double min_section_energy = std::numeric_limits<double>::infinity();
  double max_section_energy = -std::numeric_limits<double>::infinity();
  for (std::size_t segment = 0U; segment + 1U < boundaries.size(); ++segment) {
    const std::size_t first = boundaries[segment];
    const std::size_t last = boundaries[segment + 1U];
    if (first >= last || first >= windows.size()) continue;
    double energy = 0.0;
    double brightness = 0.0;
    double transients = 0.0;
    double width = 0.0;
    for (std::size_t index = first; index < last; ++index) {
      energy += windows[index].energy_db;
      brightness += windows[index].brightness;
      transients += windows[index].transient_density;
      width += windows[index].width;
    }
    const double count = static_cast<double>(last - first);
    energy /= count;
    brightness /= count;
    transients /= count;
    width /= count;
    min_section_energy = std::min(min_section_energy, energy);
    max_section_energy = std::max(max_section_energy, energy);

    std::string label = "section";
    if (segment == 0U && energy < median_energy - 2.0) label = "intro_like";
    else if (segment + 2U == boundaries.size() && energy < median_energy - 2.0) label = "outro_like";
    else if (energy > median_energy + 2.5 && transients > result.tempo.transient_fraction) label = "high_energy";
    else if (energy < median_energy - 3.0) label = "low_energy";
    else if (transients > result.tempo.transient_fraction * 1.35) label = "rhythmic_dense";

    const double boundary_strength = first > 0U && first < distances.size() ? distances[first] : 1.0;
    result.sections.push_back({.start_seconds = windows[first].start_seconds,
                               .end_seconds = windows[last - 1U].end_seconds,
                               .energy_dbfs = energy,
                               .brightness = brightness,
                               .transient_density = transients,
                               .stereo_width = width,
                               .label_hint = std::move(label),
                               .confidence = std::clamp(boundary_strength / 2.5, 0.25, 0.95)});
  }
  if (std::isfinite(min_section_energy) && std::isfinite(max_section_energy)) {
    result.macro_dynamics.section_contrast_db = std::max(0.0, max_section_energy - min_section_energy);
  }
  return result;
}

}  // namespace amt::analysis
