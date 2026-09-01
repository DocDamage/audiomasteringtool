#include "amt/reference/ReferenceInfluence.h"

#include <algorithm>
#include <cmath>

namespace amt::reference {

InfluencePlan ReferenceInfluenceEngine::compute_influence(
    const ReferenceProfile& target_reference,
    const ReferenceProfile& source_profile,
    InfluenceStrength strength) const {
  InfluencePlan plan{};
  plan.strength = strength;

  double weight = 0.50;
  double max_delta_db = 3.0;
  if (strength == InfluenceStrength::loose) {
    weight = 0.25;
    max_delta_db = 1.5;
  } else if (strength == InfluenceStrength::close) {
    weight = 0.80;
    max_delta_db = 4.5;
  }

  // Calculate target LUFS
  const double lufs_diff = target_reference.integrated_lufs - source_profile.integrated_lufs;
  plan.recommended_target_lufs = source_profile.integrated_lufs + std::clamp(lufs_diff * weight, -max_delta_db, max_delta_db);

  // Spectral deltas
  const double bass_diff = target_reference.spectrum.bass_db - source_profile.spectrum.bass_db;
  const double low_shelf_gain = std::clamp(bass_diff * weight, -max_delta_db, max_delta_db);
  plan.recommended_eq.low_shelf_gain_db = low_shelf_gain;
  plan.recommended_eq.low_shelf_freq_hz = 90.0;

  const double presence_diff = target_reference.spectrum.presence_db - source_profile.spectrum.presence_db;
  const double high_shelf_gain = std::clamp(presence_diff * weight, -max_delta_db, max_delta_db);
  plan.recommended_eq.high_shelf_gain_db = high_shelf_gain;
  plan.recommended_eq.high_shelf_freq_hz = 9000.0;

  plan.recommended_eq.pre_gain_db = (plan.recommended_target_lufs - source_profile.integrated_lufs) * 0.7;

  plan.adjustments_summary.push_back("Reference target LUFS: " + std::to_string(plan.recommended_target_lufs));
  plan.adjustments_summary.push_back("Low shelf EQ delta: " + std::to_string(low_shelf_gain) + " dB @ 90 Hz");
  plan.adjustments_summary.push_back("High shelf EQ delta: " + std::to_string(high_shelf_gain) + " dB @ 9 kHz");

  return plan;
}

}  // namespace amt::reference
