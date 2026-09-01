#pragma once

#include <string>

#include "amt/decision/CandidateRanker.h"
#include "amt/decision/Diagnosis.h"
#include "amt/decision/Evidence.h"

namespace amt::decision {

struct DecisionExplanation {
  std::string summary_text;
  std::string master_a_explanation;
  std::string master_b_explanation;
  std::string json_report;
};

class ExplanationGenerator {
 public:
  ExplanationGenerator() = default;
  ~ExplanationGenerator() = default;

  [[nodiscard]] DecisionExplanation generate_explanation(
      const TrackDiagnosis& diagnosis,
      const RankedFinalists& finalists,
      const DecisionEvidence& evidence) const;
};

}  // namespace amt::decision
