#pragma once

#include <string>
#include <vector>

namespace amt::interactions {

struct SourceActivity {
  std::string taxonomy_id;
  double confidence{0.0};
  double onset_density{0.0};
  double low_band_energy{0.0};
  double fundamental_hz{0.0};
  double transient_strength{0.0};
  double stereo_width{0.0};
  double phase_coherence{1.0};
  double active_seconds{0.0};
  double overlap_seconds{0.0};
};

struct InteractionEvidence {
  std::string first_id;
  std::string second_id;
  double confidence{0.0};
  double temporal_overlap{0.0};
  double spectral_overlap{0.0};
  double low_band_masking{0.0};
  double onset_masking{0.0};
  double phase_risk{0.0};
  double limiter_contribution{0.0};
  double mono_survival_risk{0.0};
  std::vector<std::string> evidence;
};

enum class RepairAction { none, dynamic_low_band_attenuation, transient_sidechain_control,
                          low_band_mono_stabilization, harmonic_translation_guidance };
struct RepairRecommendation { RepairAction action{RepairAction::none}; double amount{0.0}; std::string reason; };

struct DamageMetrics { double true_peak_delta_dbtp{0.0}; double loudness_delta_lu{0.0}; double transient_loss{0.0}; double spectral_side_effect{0.0}; double mono_delta{0.0}; double width_delta{0.0}; };
struct DamageGuardResult { bool safe{false}; std::vector<std::string> blockers; };

[[nodiscard]] InteractionEvidence analyze_interaction(const SourceActivity& first, const SourceActivity& second);
[[nodiscard]] std::vector<RepairRecommendation> plan_bounded_repairs(const InteractionEvidence& evidence);
[[nodiscard]] DamageGuardResult validate_repair_damage(const DamageMetrics& metrics);
[[nodiscard]] std::string repair_action_name(RepairAction action);
}  // namespace amt::interactions
