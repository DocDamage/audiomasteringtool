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

void insert_or_add(ProcessingGraph& graph, const std::string& before_id,
                   ProcessorSpec spec) {
  if (!graph.insert_before(before_id, spec)) graph.add(std::move(spec));
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

  return {.master_a = std::move(a), .master_b = std::move(b), .stem_mix = {}};
}

MasteringPlan plan_mastering(const amt::analysis::AnalysisReport& report) {
  auto plan = plan_mastering(report.technical);
  auto& a = plan.master_a;
  auto& b = plan.master_b;

  if (report.character.accidental_defect_risk >= 0.55) {
    a.target_lufs = std::max(-12.0, a.target_lufs - 0.8);
    b.target_lufs = std::max(-13.0, b.target_lufs - 0.5);
    a.graph.set_bypass("a_saturation", true);
    b.graph.set_bypass("b_light_saturation", true);
    if (report.character.hard_clip_likelihood >= 0.72) {
      a.graph.set_bypass("a_clipper", true);
      a.rationale.push_back("Avoided additional clipping because the source already shows a high hard-clip defect risk.");
    }
    a.rationale.push_back("Backed off target loudness because Phase 3 found a meaningful accidental-clipping risk.");
    b.rationale.push_back("Preservation alternative backs off further when source clipping looks accidental.");
  } else if (report.character.intentional_character_likelihood >= 0.60) {
    a.graph.set_bypass("a_saturation", true);
    b.graph.set_bypass("b_light_saturation", true);
    a.rationale.push_back("Preserved existing saturation/clipping character instead of layering more saturation on top.");
  }

  if (report.perceptual.harshness_score >= 0.58) {
    double frequency = 3800.0;
    double severity = report.perceptual.harshness_score;
    for (const auto& resonance : report.perceptual.resonances) {
      if (resonance.frequency_hz >= 2500.0 && resonance.frequency_hz <= 6500.0 &&
          resonance.severity > severity * 0.55) {
        frequency = resonance.frequency_hz;
        severity = std::max(severity, resonance.severity);
        break;
      }
    }
    add(a.graph, "a_phase3_harshness_control",
        DynamicEqParams{.frequency_hz = frequency, .q = 1.35, .threshold_db = -25.0,
                        .ratio = 1.9, .attack_ms = 6.0, .release_ms = 85.0,
                        .max_reduction_db = 1.2 + severity * 1.6});
    a.rationale.push_back("Used Phase 3 time/persistence evidence to dynamically control upper-mid harshness near " +
                          std::to_string(static_cast<int>(std::lround(frequency))) + " Hz.");
    if (severity >= 0.82) {
      add(b.graph, "b_phase3_harshness_safety",
          DynamicEqParams{.frequency_hz = frequency, .q = 1.15, .threshold_db = -23.0,
                          .ratio = 1.45, .attack_ms = 10.0, .release_ms = 110.0,
                          .max_reduction_db = 0.9});
    }
  }

  if (report.perceptual.sub_buildup_score >= 0.65 && !a.graph.contains("a_low_dynamic_eq")) {
    add(a.graph, "a_phase3_sub_control",
        DynamicEqParams{.frequency_hz = 62.0, .q = 0.9, .threshold_db = -20.0,
                        .ratio = 2.0, .attack_ms = 22.0, .release_ms = 170.0,
                        .max_reduction_db = 2.2});
    a.rationale.push_back("Added narrow, event-reactive sub control because Phase 3 found persistent sub buildup.");
  }

  if (report.structural.macro_dynamics.section_contrast_db >= 8.0) {
    a.target_lufs = std::max(-12.0, a.target_lufs - 0.4);
    b.target_lufs = std::max(-13.0, b.target_lufs - 0.25);
    a.rationale.push_back("Reduced loudness pressure slightly to preserve strong section-to-section arrangement contrast.");
  }

  if (report.technical.metadata.channels == 2 &&
      (report.technical.stereo.correlation < 0.05 ||
       report.technical.stereo.negative_correlation_window_fraction > 0.18)) {
    add(a.graph, "a_phase3_phase_safety", StereoParams{.width = 0.92, .bass_mono_hz = 120.0});
    add(b.graph, "b_phase3_phase_safety", StereoParams{.width = 0.96, .bass_mono_hz = 110.0});
    a.rationale.push_back("Reduced stereo risk because Phase 3 found recurring negative-correlation behavior.");
  }

  if ((report.technical.loudness.crest_factor_db < 6.5 ||
       report.technical.loudness.peak_to_loudness_ratio_db < 6.0) &&
      !a.graph.contains("a_transient_restore")) {
    add(a.graph, "a_phase3_transient_restore",
        TransientParams{.attack_db = 0.9, .sustain_db = -0.1,
                        .fast_ms = 3.0, .slow_ms = 34.0, .mix = 0.60});
    a.rationale.push_back("Restored a small amount of attack because Phase 3 found already-flattened transient contrast.");
  }

  if (report.mix_health.overall_heuristic_score >= 86.0 &&
      report.character.accidental_defect_risk < 0.25) {
    a.target_lufs = std::min(a.target_lufs, -9.8);
    a.graph.set_bypass("a_multiband_control", true);
    a.graph.set_bypass("a_saturation", true);
    a.rationale.push_back("The source already scores strongly across deterministic Mix Health dimensions, so broad processing was reduced.");
  }

  a.target_lufs = std::clamp(a.target_lufs, -12.0, -8.5);
  b.target_lufs = std::clamp(b.target_lufs, -13.0, -9.5);
  return plan;
}

MasteringControls mastering_style_preset(const MasteringStyle style) {
  MasteringControls controls;
  controls.style = style;
  switch (style) {
    case MasteringStyle::balanced:
      return controls;
    case MasteringStyle::transparent:
      controls.target_lufs = -13.0;
      controls.punch = 0.35;
      controls.warmth = 0.0;
      return controls;
    case MasteringStyle::punchy:
      controls.target_lufs = -10.5;
      controls.bass_db = 0.3;
      controls.presence_db = 0.2;
      controls.width = 1.04;
      controls.punch = 0.85;
      controls.warmth = 0.15;
      return controls;
    case MasteringStyle::warm:
      controls.target_lufs = -11.5;
      controls.bass_db = 0.8;
      controls.presence_db = -0.5;
      controls.punch = 0.45;
      controls.warmth = 0.65;
      return controls;
    case MasteringStyle::wide:
      controls.target_lufs = -11.5;
      controls.bass_db = -0.2;
      controls.presence_db = 0.3;
      controls.width = 1.14;
      controls.punch = 0.5;
      return controls;
    case MasteringStyle::loud:
      controls.target_lufs = -9.0;
      controls.presence_db = 0.3;
      controls.width = 1.03;
      controls.punch = 0.7;
      controls.warmth = 0.3;
      return controls;
  }
  return controls;
}

void apply_mastering_controls(MasteringPlan& plan,
                              const MasteringControls& requested) {
  const double target = std::clamp(requested.target_lufs, -14.0, -8.0);
  const double bass = std::clamp(requested.bass_db, -3.0, 3.0);
  const double presence = std::clamp(requested.presence_db, -3.0, 3.0);
  const double width = std::clamp(requested.width, 0.80, 1.20);
  const double punch = std::clamp(requested.punch, 0.0, 1.0);
  const double warmth = std::clamp(requested.warmth, 0.0, 1.0);

  plan.master_a.target_lufs = target;
  plan.master_b.target_lufs = std::clamp(target - 1.5, -14.0, -9.5);
  plan.stem_mix = {
      .drums_db = std::clamp(requested.stem_mix.drums_db, -12.0, 6.0),
      .bass_db = std::clamp(requested.stem_mix.bass_db, -12.0, 6.0),
      .vocals_db = std::clamp(requested.stem_mix.vocals_db, -12.0, 6.0),
      .other_db = std::clamp(requested.stem_mix.other_db, -12.0, 6.0)};

  auto apply = [&](MasteringCandidatePlan& candidate, const std::string& prefix,
                   const std::string& dynamics_anchor,
                   const std::string& peak_anchor, const double scale) {
    std::vector<EqBand> tone;
    if (std::abs(bass) >= 0.05) {
      tone.push_back({.shape = EqShape::low_shelf, .frequency_hz = 105.0,
                      .gain_db = bass * scale, .q = 0.707});
    }
    if (std::abs(presence) >= 0.05) {
      tone.push_back({.shape = EqShape::peak, .frequency_hz = 4200.0,
                      .gain_db = presence * scale, .q = 0.85});
    }
    if (!tone.empty()) {
      insert_or_add(candidate.graph, dynamics_anchor,
                    ProcessorSpec{.id = prefix + "user_tone",
                                  .bypass = false,
                                  .params = EqParams{.bands = std::move(tone)}});
    }

    const double attack_db = (punch - 0.5) * 3.0 * scale;
    if (std::abs(attack_db) >= 0.08) {
      insert_or_add(candidate.graph, peak_anchor,
                    ProcessorSpec{.id = prefix + "user_punch",
                                  .bypass = false,
                                  .params = TransientParams{
                                      .attack_db = attack_db,
                                      .sustain_db = -attack_db * 0.12,
                                      .fast_ms = 3.0,
                                      .slow_ms = 34.0,
                                      .mix = 0.72}});
    }

    if (std::abs(width - 1.0) >= 0.005) {
      insert_or_add(candidate.graph, peak_anchor,
                    ProcessorSpec{.id = prefix + "user_width",
                                  .bypass = false,
                                  .params = StereoParams{
                                      .width = 1.0 + (width - 1.0) * scale,
                                      .bass_mono_hz = 105.0}});
    }

    if (warmth >= 0.01) {
      insert_or_add(candidate.graph, peak_anchor,
                    ProcessorSpec{.id = prefix + "user_warmth",
                                  .bypass = false,
                                  .params = SaturationParams{
                                      .drive_db = 0.8 + warmth * 2.8,
                                      .mix = warmth * 0.12 * scale}});
    }

    candidate.rationale.push_back(
        "Applied user mastering controls for loudness, tone, dynamics, width, and warmth before final peak protection.");
  };

  apply(plan.master_a, "a_", "a_glue", "a_clipper", 1.0);
  apply(plan.master_b, "b_", "b_glue", "b_limiter", 0.65);
}

}  // namespace amt::mastering
