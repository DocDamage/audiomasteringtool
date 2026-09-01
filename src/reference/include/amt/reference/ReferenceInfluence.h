#pragma once

#include "amt/mastering/Planner.h"
#include "amt/reference/ReferenceProfile.h"

namespace amt::reference {

enum class InfluenceStrength {
  loose,   // 25% match weight, conservative bounds (max +/-1.5 dB)
  medium,  // 50% match weight, balanced bounds (max +/-3.0 dB)
  close    // 80% match weight, aggressive bounds (max +/-4.5 dB), strictly guarded
};

struct ReferenceEqAdjustment {
  double pre_gain_db{0.0};
  double low_shelf_gain_db{0.0};
  double low_shelf_freq_hz{90.0};
  double high_shelf_gain_db{0.0};
  double high_shelf_freq_hz{9000.0};
};

struct InfluencePlan {
  InfluenceStrength strength{InfluenceStrength::medium};
  ReferenceEqAdjustment recommended_eq;
  double recommended_target_lufs{-14.0};
  std::vector<std::string> adjustments_summary;
};

class ReferenceInfluenceEngine {
 public:
  ReferenceInfluenceEngine() = default;
  ~ReferenceInfluenceEngine() = default;

  [[nodiscard]] InfluencePlan compute_influence(
      const ReferenceProfile& target_reference,
      const ReferenceProfile& source_profile,
      InfluenceStrength strength = InfluenceStrength::medium) const;
};

}  // namespace amt::reference
