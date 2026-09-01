#pragma once

#include <utility>
#include <vector>

#include "amt/decision/CandidateGenerator.h"
#include "amt/decision/CandidateScorer.h"
#include "amt/mastering/Planner.h"

namespace amt::decision {

struct RankedFinalists {
  MasterCandidateProfile master_a_recommended;
  MasterCandidateProfile master_b_alternative;
  CandidateScore master_a_score;
  CandidateScore master_b_score;
};

class CandidateRanker {
 public:
  CandidateRanker() = default;
  ~CandidateRanker() = default;

  [[nodiscard]] RankedFinalists select_finalists(
      const std::vector<MasterCandidateProfile>& candidates,
      const DecisionEvidence& evidence,
      const MasteringConstraints& constraints = {}) const;
};

}  // namespace amt::decision
