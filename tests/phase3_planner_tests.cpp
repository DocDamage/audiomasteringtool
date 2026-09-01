#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>

#include "amt/analysis/DeepAnalysis.h"
#include "amt/mastering/Planner.h"

namespace {

const amt::dsp::ProcessorSpec* find_node(
    const amt::mastering::ProcessingGraph& graph, const std::string& id) {
  const auto& nodes = graph.nodes();
  const auto iterator = std::find_if(nodes.begin(), nodes.end(),
      [&](const auto& node) { return node.id == id; });
  return iterator == nodes.end() ? nullptr : &*iterator;
}

amt::analysis::AnalysisReport make_report() {
  amt::analysis::AnalysisReport report;
  report.technical.metadata.sample_rate = 48000;
  report.technical.metadata.channels = 2;
  report.technical.loudness.integrated_lufs = -14.0;
  report.technical.loudness.true_peak_dbtp = -2.0;
  report.technical.loudness.crest_factor_db = 9.0;
  report.technical.loudness.peak_to_loudness_ratio_db = 8.5;
  report.technical.loudness.short_term_variation_lu = 4.0;
  report.technical.spectrum.centroid_hz = 3100.0;
  report.technical.spectrum.bands = {
      {.low_hz = 20.0, .high_hz = 60.0, .energy_ratio = 0.12},
      {.low_hz = 60.0, .high_hz = 250.0, .energy_ratio = 0.26},
      {.low_hz = 250.0, .high_hz = 500.0, .energy_ratio = 0.09},
      {.low_hz = 500.0, .high_hz = 2000.0, .energy_ratio = 0.22},
      {.low_hz = 2000.0, .high_hz = 6000.0, .energy_ratio = 0.21},
      {.low_hz = 6000.0, .high_hz = 20000.0, .energy_ratio = 0.10}};
  report.technical.stereo.correlation = -0.08;
  report.technical.stereo.negative_correlation_window_fraction = 0.28;
  report.technical.stereo.low_band_width = 0.31;
  report.technical.stereo.mid_band_width = 0.48;
  report.character.intentional_character_likelihood = 0.72;
  report.character.accidental_defect_risk = 0.12;
  report.character.hard_clip_likelihood = 0.30;
  report.character.inference_confidence = 0.90;
  report.perceptual.harshness_score = 0.84;
  report.perceptual.sub_buildup_score = 0.70;
  report.perceptual.resonances.push_back(
      {.frequency_hz = 3475.0, .prominence_db = 7.5, .persistence = 0.68,
       .severity = 0.90, .first_seen_seconds = 12.0, .last_seen_seconds = 44.0});
  report.structural.macro_dynamics.section_contrast_db = 10.5;
  report.mix_health.overall_heuristic_score = 68.0;
  return report;
}

void test_deep_planner_changes_strategy() {
  auto report = make_report();
  const auto baseline = amt::mastering::plan_mastering(report.technical);
  const auto deep = amt::mastering::plan_mastering(report);

  assert(deep.master_a.target_lufs < baseline.master_a.target_lufs);
  assert(deep.master_a.graph.contains("a_phase3_harshness_control"));
  assert(deep.master_a.graph.contains("a_phase3_phase_safety"));
  assert(deep.master_b.graph.contains("b_phase3_harshness_safety"));
  assert(deep.master_b.graph.contains("b_phase3_phase_safety"));

  const auto* saturation = find_node(deep.master_a.graph, "a_saturation");
  assert(saturation != nullptr);
  assert(saturation->bypass);

  const auto* harshness = find_node(deep.master_a.graph, "a_phase3_harshness_control");
  assert(harshness != nullptr && !harshness->bypass);
  const auto* params = std::get_if<amt::dsp::DynamicEqParams>(&harshness->params);
  assert(params != nullptr);
  assert(std::abs(params->frequency_hz - 3475.0) < 1.0);
}

void test_accidental_clipping_backs_off() {
  auto report = make_report();
  report.character.intentional_character_likelihood = 0.10;
  report.character.accidental_defect_risk = 0.82;
  report.character.hard_clip_likelihood = 0.90;
  const auto deep = amt::mastering::plan_mastering(report);
  const auto* clipper = find_node(deep.master_a.graph, "a_clipper");
  assert(clipper != nullptr && clipper->bypass);
  assert(deep.master_a.target_lufs <= -10.0);
}

}  // namespace

int main() {
  test_deep_planner_changes_strategy();
  test_accidental_clipping_backs_off();
  return 0;
}
