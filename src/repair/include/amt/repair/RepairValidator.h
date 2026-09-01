#pragma once

#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/repair/RepairPolicy.h"
#include "amt/repair/RepairProposal.h"

namespace amt::repair {

struct ValidationResult {
  bool passed{false};
  double peak_delta_dbtp{0.0};
  double spectral_difference{0.0};
  double transient_loss{0.0};
  std::vector<std::string> reasons;
};

class RepairValidator {
 public:
  RepairValidator() = default;
  ~RepairValidator() = default;

  [[nodiscard]] ValidationResult validate(
      const amt::audio::AudioBuffer& original,
      const amt::audio::AudioBuffer& repaired,
      const RepairPolicy& policy = {}) const;
};

}  // namespace amt::repair
