#pragma once

#include "amt/decision/CandidateGenerator.h"
#include "amt/decision/Evidence.h"

namespace amt::decision {

struct CandidateScore {
  double tonal_balance{0.0};
  double low_end_stability{0.0};
  double punch_and_transients{0.0};
  double loudness_density{0.0};
  double artifact_risk{0.0};
  double stereo_integrity{1.0};
  double overall_score{0.0};
};

class CandidateScorer {
 public:
  CandidateScorer() = default;
  ~CandidateScorer() = default;

  [[nodiscard]] CandidateScore score_candidate(
      const MasterCandidateProfile& candidate,
      const DecisionEvidence& evidence,
      const MasteringConstraints& constraints = {}) const;
};

}  // namespace amt::decision
