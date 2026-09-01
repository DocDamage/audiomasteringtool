#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <string>

#include "amt/analysis/FileAnalyzer.h"
#include "amt/audio/AudioBuffer.h"
#include "amt/dsp/Processors.h"
#include "amt/mastering/Audition.h"
#include "amt/mastering/Planner.h"
#include "amt/mastering/ProcessingGraph.h"

namespace {

double max_abs(const amt::audio::AudioBuffer& buffer) {
  double peak = 0.0;
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    for (const float sample : buffer.channel(channel)) peak = std::max(peak, std::abs(static_cast<double>(sample)));
  }
  return peak;
}

}  // namespace

int main() {
  amt::audio::AudioBuffer buffer(2U, 4096U);
  for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
    const double value = 0.55 * std::sin(2.0 * std::numbers::pi * 440.0 *
                                        static_cast<double>(frame) / 48000.0);
    buffer.channel(0U)[frame] = static_cast<float>(value);
    buffer.channel(1U)[frame] = static_cast<float>(value);
  }

  amt::mastering::ProcessingGraph graph;
  graph.add({.id = "gain", .bypass = false, .params = amt::dsp::GainParams{.gain_db = 9.0}});
  graph.add({.id = "clip", .bypass = false,
             .params = amt::dsp::ClipperParams{.threshold_db = -3.0, .softness = 0.2}});
  graph.add({.id = "limit", .bypass = false,
             .params = amt::dsp::LimiterParams{.ceiling_db = -3.0, .release_ms = 80.0}});
  std::string error;
  assert(graph.validate(error));
  assert(graph.to_json().find("\"type\":\"limiter\"") != std::string::npos);

  amt::mastering::ProcessingGraphRuntime runtime(graph, 48000, 2U);
  runtime.process(buffer);
  assert(max_abs(buffer) <= std::pow(10.0, -3.0 / 20.0) + 1.0e-5);

  amt::mastering::ProcessingGraph duplicate;
  duplicate.add({.id = "x", .params = amt::dsp::GainParams{}});
  duplicate.add({.id = "x", .params = amt::dsp::GainParams{}});
  assert(!duplicate.validate(error));

  amt::analysis::Phase1AnalysisReport report;
  report.metadata.sample_rate = 48000;
  report.metadata.channels = 2;
  report.loudness.integrated_lufs = -15.5;
  report.loudness.true_peak_dbtp = -2.0;
  report.loudness.crest_factor_db = 11.5;
  report.loudness.peak_to_loudness_ratio_db = 11.0;
  report.loudness.short_term_variation_lu = 6.5;
  report.spectrum.centroid_hz = 2800.0;
  report.spectrum.bands = {
      {.low_hz = 20.0, .high_hz = 60.0, .energy_ratio = 0.12},
      {.low_hz = 60.0, .high_hz = 250.0, .energy_ratio = 0.38},
      {.low_hz = 250.0, .high_hz = 500.0, .energy_ratio = 0.08},
      {.low_hz = 500.0, .high_hz = 2000.0, .energy_ratio = 0.20},
      {.low_hz = 2000.0, .high_hz = 6000.0, .energy_ratio = 0.16},
      {.low_hz = 6000.0, .high_hz = 20000.0, .energy_ratio = 0.06}};
  report.stereo.correlation = 0.82;
  report.stereo.low_band_width = 0.24;
  report.stereo.mid_band_width = 0.22;
  report.integrity.clipped_samples = 0;
  report.integrity.max_absolute_dc_offset = 0.0;

  const auto plan = amt::mastering::plan_mastering(report);
  assert(plan.master_a.recommended);
  assert(!plan.master_b.recommended);
  assert(plan.master_a.graph.validate(error));
  assert(plan.master_b.graph.validate(error));
  assert(plan.master_a.graph.to_json() != plan.master_b.graph.to_json());
  assert(plan.master_b.preservation_bias > plan.master_a.preservation_bias);
  assert(plan.master_b.target_lufs <= plan.master_a.target_lufs);

  amt::analysis::LoudnessMetrics original;
  amt::analysis::LoudnessMetrics a;
  amt::analysis::LoudnessMetrics b;
  original.integrated_lufs = -15.0;
  a.integrated_lufs = -9.5;
  b.integrated_lufs = -11.5;
  const auto audition = amt::mastering::make_loudness_match_profile(original, a, b);
  assert(std::abs(audition.reference_lufs + 15.0) < 1.0e-9);
  assert(std::abs(audition.original_gain_db) < 1.0e-9);
  assert(audition.master_a_gain_db < audition.master_b_gain_db);
  assert(audition.master_a_gain_db <= 0.0 && audition.master_b_gain_db <= 0.0);

  return 0;
}
