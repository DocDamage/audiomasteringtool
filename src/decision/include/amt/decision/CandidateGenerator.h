#pragma once

#include <string>
#include <vector>

#include "amt/decision/Evidence.h"
#include "amt/decision/MasteringConstraints.h"
#include "amt/mastering/Planner.h"

namespace amt::decision {

struct MasterCandidateProfile {
  std::string profile_id;
  std::string display_name;
  std::string philosophy;
  amt::mastering::MasteringCandidatePlan branch_settings;
};

class CandidateGenerator {
 public:
  CandidateGenerator() = default;
  ~CandidateGenerator() = default;

  [[nodiscard]] std::vector<MasterCandidateProfile> generate_candidates(
      const DecisionEvidence& evidence,
      const MasteringConstraints& constraints = {}) const;
};

}  // namespace amt::decision
