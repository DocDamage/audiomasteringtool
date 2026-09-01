#include "amt/mastering/Planner.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace amt::mastering {
namespace {

using amt::dsp::ClipperParams;
using amt::dsp::CompressorParams;
using amt::dsp::DynamicEqParams;
using amt::dsp::EqBand;
using amt::dsp::EqParams;
using amt::dsp::EqShape;
using amt::dsp::GainParams;
using amt::dsp::LimiterParams;
using amt::dsp::MultibandParams;
using amt::dsp::ProcessorSpec;
using amt::dsp::SaturationParams;
using amt::dsp::StereoParams;
using amt::dsp::TransientParams;

double band_ratio(const amt::analysis::SpectrumMetrics& spectrum, const std::size_t index) {
  return index < spectrum.bands.size() ? spectrum.bands[index].energy_ratio : 0.0;
}

void add(ProcessingGraph& graph, std::string id, amt::dsp::ProcessorParams params) {
  graph.add(ProcessorSpec{.id = std::move(id), .bypass = false, .params = std::move(params)});
}

double reliable_lufs(const double value) {
  return std::isfinite(value) && value > -80.0 && value < 6.0 ? value : -14.0;
}

}  // namespace

MasteringPlan plan_mastering(const amt::analysis::Phase1AnalysisReport& report) {
  const double current_lufs = reliable_lufs(report.loudness.integrated_lufs);
  const double crest = std::clamp(report.loudness.crest_factor_db, 2.0, 24.0);
  const double plr = std::clamp(report.loudness.peak_to_loudness_ratio_db, 2.0, 24.0);
  const double sub = band_ratio(report.spectrum, 0U);
  const double bass = band_ratio(report.spectrum, 1U);
  const double upper_mid = band_ratio(report.spectrum, 4U);
  const double high = band_ratio(report.spectrum, 5U);
  const double low_end = sub + bass;

  double target_a = -10.0;
  if (crest >= 12.0 && plr >= 10.0) target_a = -9.2;
  if (crest <= 7.0 || plr <= 7.0) target_a = -10.8;
  if (current_lufs >= -9.0) target_a = current_lufs;
  target_a = std::clamp(target_a, -12.0, -8.5);

  double target_b = current_lufs >= -10.5 ? current_lufs : target_a - 1.5;
  target_b = std::clamp(target_b, -13.0, -9.5);

  MasteringCandidatePlan a;
  a.id = "master_a";
  a.name = "Recommended";
  a.recommended = true;
  a.target_lufs = target_a;
  a.ceiling_dbtp = -1.0;
  a.preservation_bias = 0.35;

  double pre_gain_a = std::clamp((target_a - current_lufs) * 0.68 - 0.6, -3.0, 6.0);
  if (report.loudness.true_peak_dbtp > 0.0) pre_gain_a -= std::min(report.loudness.true_peak_dbtp, 2.0);
  add(a.graph, "a_pre_gain", GainParams{.gain_db = pre_gain_a});

  std::vector<EqBand> corrective_eq;
  if (report.integrity.max_absolute_dc_offset > 0.003) {
    corrective_eq.push_back({.shape = EqShape::high_pass, .frequency_hz = 18.0, .gain_db = 0.0, .q = 0.707});
    a.rationale.push_back("Removed inaudible DC/ultra-low buildup before dynamics.");
  }
  if (low_end > 0.48) {
    corrective_eq.push_back({.shape = EqShape::low_shelf, .frequency_hz = 105.0, .gain_db = -1.2, .q = 0.707});
    a.rationale.push_back("Reduced excess low-end weight before final loudness processing.");
  } else if (low_end < 0.20) {
    corrective_eq.push_back({.shape = EqShape::low_shelf, .frequency_hz = 95.0, .gain_db = 0.6, .q = 0.707});
    a.rationale.push_back("Added a small amount of low-end body.");
  }
  if (upper_mid > 0.23) {
    corrective_eq.push_back({.shape = EqShape::peak, .frequency_hz = 3500.0, .gain_db = -0.8, .q = 0.9});
    a.rationale.push_back("Softened dense upper-mid energy before limiting.");
  }
  if (high > 0.13 && report.spectrum.centroid_hz > 3200.0) {
    corrective_eq.push_back({.shape = EqShape::high_shelf, .frequency_hz = 8500.0, .gain_db = -0.6, .q = 0.707});
    a.rationale.push_back("Controlled top-end brightness without broadly darkening the mix.");
  } else if (high < 0.045 && report.spectrum.centroid_hz < 2200.0) {
    corrective_eq.push_back({.shape = EqShape::high_shelf, .frequency_hz = 9000.0, .gain_db = 0.45, .q = 0.707});
    a.rationale.push_back("Added a restrained amount of high-frequency openness.");
  }
  if (!corrective_eq.empty()) add(a.graph, "a_tonal_eq", EqParams{.bands = corrective_eq});

  if (low_end > 0.42) {
    add(a.graph, "a_low_dynamic_eq",
        DynamicEqParams{.frequency_hz = 70.0, .q = 1.0, .threshold_db = -20.0,
                        .ratio = 2.0, .attack_ms = 18.0, .release_ms = 150.0,
                        .max_reduction_db = low_end > 0.55 ? 3.0 : 2.0});
    a.rationale.push_back("Used event-reactive low-frequency control instead of a large static bass cut.");
  }
  if (upper_mid > 0.20) {
    add(a.graph, "a_upper_mid_dynamic_eq",
        DynamicEqParams{.frequency_hz = 3800.0, .q = 1.2, .threshold_db = -24.0,
                        .ratio = 1.8, .attack_ms = 8.0, .release_ms = 90.0,
                        .max_reduction_db = 1.8});
  }

  const double compressor_ratio = crest < 8.0 ? 1.25 : 1.55;
  add(a.graph, "a_glue",
      CompressorParams{.threshold_db = -17.0, .ratio = compressor_ratio,
                       .attack_ms = crest < 8.0 ? 35.0 : 24.0, .release_ms = 120.0,
                       .knee_db = 6.0, .makeup_db = 0.0, .mix = crest < 8.0 ? 0.45 : 0.7});

  if (report.loudness.short_term_variation_lu > 5.0 || low_end > 0.52) {
    add(a.graph, "a_multiband_control",
        MultibandParams{.low_crossover_hz = 125.0, .high_crossover_hz = 5200.0,
                        .low_threshold_db = -19.0, .mid_threshold_db = -16.0,
                        .high_threshold_db = -18.0, .low_ratio = 1.45,
                        .mid_ratio = 1.2, .high_ratio = 1.3,
                        .attack_ms = 28.0, .release_ms = 170.0});
    a.rationale.push_back("Stabilized section-to-section density without forcing a single-band compressor harder.");
  }

  if (crest < 9.0) {
    add(a.graph, "a_transient_restore",
        TransientParams{.attack_db = 1.25, .sustain_db = -0.15,
                        .fast_ms = 3.0, .slow_ms = 32.0, .mix = 0.75});
    a.rationale.push_back("Restored transient contrast before the final clip/limit stages.");
  }

  if (report.integrity.clipped_samples == 0 && report.loudness.true_peak_dbtp < 0.5) {
    add(a.graph, "a_saturation", SaturationParams{.drive_db = 2.0, .mix = 0.10});
    a.rationale.push_back("Added restrained harmonic density rather than relying on limiting alone.");
  }

  double width_a = 1.0;
  if (report.metadata.channels == 2) {
    if (report.stereo.correlation > 0.75 && report.stereo.mid_band_width < 0.35) width_a = 1.08;
    if (report.stereo.correlation < 0.20) width_a = 0.95;
    add(a.graph, "a_stereo",
        StereoParams{.width = width_a,
                     .bass_mono_hz = report.stereo.low_band_width > 0.15 ? 115.0 : 85.0});
    if (report.stereo.low_band_width > 0.15) {
      a.rationale.push_back("Kept the sub/low bass centered while preserving width above it.");
    }
  }

  add(a.graph, "a_clipper",
      ClipperParams{.threshold_db = target_a > -10.2 ? -1.7 : -1.25,
                    .softness = target_a > -10.2 ? 0.32 : 0.20});
  add(a.graph, "a_limiter", LimiterParams{.ceiling_db = a.ceiling_dbtp, .release_ms = 85.0});
  a.rationale.push_back("Used staged clip/limit control for commercial level while protecting transient shape.");

  MasteringCandidatePlan b;
  b.id = "master_b";
  b.name = "Preservation Alternative";
  b.recommended = false;
  b.target_lufs = target_b;
  b.ceiling_dbtp = -1.0;
  b.preservation_bias = 0.82;

  double pre_gain_b = std::clamp((target_b - current_lufs) * 0.48 - 0.25, -2.0, 3.5);
  if (report.loudness.true_peak_dbtp > 0.0) pre_gain_b -= std::min(report.loudness.true_peak_dbtp, 1.5);
  add(b.graph, "b_pre_gain", GainParams{.gain_db = pre_gain_b});

  std::vector<EqBand> preservation_eq;
  if (low_end > 0.52) preservation_eq.push_back(
      {.shape = EqShape::low_shelf, .frequency_hz = 100.0, .gain_db = -0.55, .q = 0.707});
  if (upper_mid > 0.26) preservation_eq.push_back(
      {.shape = EqShape::peak, .frequency_hz = 3600.0, .gain_db = -0.4, .q = 0.8});
  if (!preservation_eq.empty()) add(b.graph, "b_tonal_eq", EqParams{.bands = preservation_eq});

  add(b.graph, "b_glue",
      CompressorParams{.threshold_db = -14.0, .ratio = 1.22,
                       .attack_ms = 40.0, .release_ms = 180.0,
                       .knee_db = 8.0, .makeup_db = 0.0, .mix = 0.42});

  if (report.metadata.channels == 2 && report.stereo.low_band_width > 0.22) {
    add(b.graph, "b_low_end_stereo_safety",
        StereoParams{.width = 1.0, .bass_mono_hz = 100.0});
  }
  if (report.integrity.clipped_samples == 0 && crest > 8.0) {
    add(b.graph, "b_light_saturation", SaturationParams{.drive_db = 1.0, .mix = 0.04});
  }
  add(b.graph, "b_limiter", LimiterParams{.ceiling_db = b.ceiling_dbtp, .release_ms = 120.0});

  b.rationale.push_back("Keeps tonal and dynamic intervention deliberately lighter than Master A.");
  b.rationale.push_back("Uses gentle glue plus peak control instead of clipper-led loudness shaping.");

  return {.master_a = std::move(a), .master_b = std::move(b)};
}

}  // namespace amt::mastering
