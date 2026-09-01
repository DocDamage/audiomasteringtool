#include "amt/decision/CandidateRanker.h"

#include <algorithm>

namespace amt::decision {

RankedFinalists CandidateRanker::select_finalists(
    const std::vector<MasterCandidateProfile>& candidates,
    const DecisionEvidence& evidence,
    const MasteringConstraints& constraints) const {
  RankedFinalists result{};
  if (candidates.empty()) return result;

  CandidateScorer scorer;
  std::vector<std::pair<MasterCandidateProfile, CandidateScore>> scored;
  scored.reserve(candidates.size());

  for (const auto& cand : candidates) {
    auto score = scorer.score_candidate(cand, evidence, constraints);
    scored.emplace_back(cand, score);
  }

  // Sort by overall score descending
  std::sort(scored.begin(), scored.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.second.overall_score > rhs.second.overall_score;
  });

  result.master_a_recommended = scored.front().first;
  result.master_a_recommended.branch_settings.name = "Master A";
  result.master_a_score = scored.front().second;

  if (scored.size() > 1) {
    result.master_b_alternative = scored[1].first;
  } else {
    result.master_b_alternative = scored.front().first;
  }
  result.master_b_alternative.branch_settings.name = "Master B";
  result.master_b_score = scored.size() > 1 ? scored[1].second : scored.front().second;

  return result;
}

}  // namespace amt::decision
