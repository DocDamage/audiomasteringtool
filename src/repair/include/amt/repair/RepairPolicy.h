#pragma once

#include "amt/repair/RepairProposal.h"

namespace amt::repair {

struct RepairPolicy {
  bool enable_auto_repairs{true};
  double max_declipping_gain_db{3.0};
  double max_transient_boost_db{4.0};
  double max_denoise_reduction_db{6.0};
  double max_spectral_side_effect_threshold{0.35};
  double min_confidence_threshold{0.65};
};

[[nodiscard]] inline bool is_proposal_safe(
    const RepairProposal& proposal,
    const RepairPolicy& policy) noexcept {
  if (proposal.confidence < policy.min_confidence_threshold) return false;
  if (proposal.repair == RepairType::declipping &&
      proposal.parameters.depth_percent > policy.max_declipping_gain_db * 33.3) {
    return false;
  }
  if (proposal.repair == RepairType::localized_spectral_denoise &&
      proposal.parameters.depth_percent > policy.max_denoise_reduction_db * 16.6) {
    return false;
  }
  return true;
}

}  // namespace amt::repair
