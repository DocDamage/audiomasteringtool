#include "amt/interactions/InteractionEngine.h"

#include <algorithm>
#include <cmath>

namespace amt::interactions {
namespace { double unit(double value) { return std::clamp(value, 0.0, 1.0); } }
InteractionEvidence analyze_interaction(const SourceActivity& first, const SourceActivity& second) {
  InteractionEvidence result; result.first_id = first.taxonomy_id; result.second_id = second.taxonomy_id;
  result.confidence = unit(std::min(first.confidence, second.confidence));
  const auto shorter = std::max(0.001, std::min(first.active_seconds, second.active_seconds));
  result.temporal_overlap = unit(std::min(first.overlap_seconds, second.overlap_seconds) / shorter);
  const auto ratio = std::min(first.low_band_energy, second.low_band_energy) / std::max(0.001, std::max(first.low_band_energy, second.low_band_energy));
  result.spectral_overlap = unit(ratio * (std::abs(first.fundamental_hz - second.fundamental_hz) < 35.0 ? 1.0 : 0.55));
  result.low_band_masking = unit(result.temporal_overlap * result.spectral_overlap);
  result.onset_masking = unit(result.temporal_overlap * std::min(first.transient_strength, second.transient_strength));
  result.phase_risk = unit(1.0 - std::min(first.phase_coherence, second.phase_coherence));
  result.mono_survival_risk = unit(result.phase_risk * std::max(first.stereo_width, second.stereo_width));
  result.limiter_contribution = unit(result.low_band_masking * (first.transient_strength + second.transient_strength) * 0.5);
  if (result.low_band_masking > 0.35) result.evidence.push_back("time-local low-band overlap is high");
  if (result.phase_risk > 0.30) result.evidence.push_back("low-frequency phase coherence is weak");
  return result;
}
std::vector<RepairRecommendation> plan_bounded_repairs(const InteractionEvidence& e) {
  std::vector<RepairRecommendation> output;
  if (e.confidence < 0.70) return output;
  if (e.low_band_masking > 0.42) output.push_back({RepairAction::dynamic_low_band_attenuation, std::min(2.0, e.low_band_masking * 3.0), "calibrated kick/bass low-band masking evidence"});
  if (e.onset_masking > 0.50) output.push_back({RepairAction::transient_sidechain_control, std::min(0.35, e.onset_masking * 0.4), "overlapping transient evidence"});
  if (e.mono_survival_risk > 0.32) output.push_back({RepairAction::low_band_mono_stabilization, std::min(0.30, e.mono_survival_risk * 0.5), "phase/mono survival evidence"});
  return output;
}
DamageGuardResult validate_repair_damage(const DamageMetrics& m) {
  DamageGuardResult r;
  r.safe = true;
  if (std::abs(m.true_peak_delta_dbtp) > 0.20) r.blockers.push_back("true-peak change exceeds 0.20 dBTP");
  if (std::abs(m.loudness_delta_lu) > 0.30) r.blockers.push_back("loudness change exceeds 0.30 LU");
  if (m.transient_loss > 0.10) r.blockers.push_back("transient loss exceeds guard");
  if (m.spectral_side_effect > 0.15) r.blockers.push_back("spectral side effect exceeds guard");
  if (m.mono_delta > 0.10 || m.width_delta > 0.12) r.blockers.push_back("stereo compatibility change exceeds guard");
  r.safe = r.blockers.empty(); return r;
}
std::string repair_action_name(const RepairAction a) { switch (a) { case RepairAction::dynamic_low_band_attenuation:return "dynamic_low_band_attenuation"; case RepairAction::transient_sidechain_control:return "transient_sidechain_control"; case RepairAction::low_band_mono_stabilization:return "low_band_mono_stabilization"; case RepairAction::harmonic_translation_guidance:return "harmonic_translation_guidance"; case RepairAction::none:return "none"; } return "none"; }
}  // namespace amt::interactions
