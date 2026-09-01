#pragma once

#include <string>
#include <vector>

namespace amt::repair {

enum class IssueType {
  none,
  peak_clipping,
  transient_smearing,
  low_end_mud,
  high_frequency_harshness,
  background_noise_floor,
  stereo_phase_cancellation
};

enum class RepairType {
  none,
  declipping,
  transient_reconstruction,
  localized_spectral_denoise,
  low_band_mono_stabilizer,
  dynamic_resonance_suppression
};

struct RepairParameters {
  double threshold_db{0.0};
  double depth_percent{0.0};
  double frequency_low_hz{20.0};
  double frequency_high_hz{20000.0};
  double blend_ratio{1.0};
};

struct RepairProposal {
  IssueType issue{IssueType::none};
  RepairType repair{RepairType::none};
  double confidence{0.0};
  double severity{0.0};
  RepairParameters parameters;
  std::string rationale;
  bool requires_user_confirmation{false};
};

[[nodiscard]] std::string issue_type_name(IssueType type);
[[nodiscard]] std::string repair_type_name(RepairType type);

}  // namespace amt::repair
