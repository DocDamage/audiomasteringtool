#include "amt/decision/CandidateScorer.h"

#include <algorithm>
#include <cmath>

namespace amt::decision {

CandidateScore CandidateScorer::score_candidate(
    const MasterCandidateProfile& candidate,
    const DecisionEvidence& evidence,
    const MasteringConstraints& constraints) const {
  CandidateScore score{};

  // Tonal balance scoring
  score.tonal_balance = 0.85;
  if (evidence.spectral_centroid_hz > 4000.0 && candidate.branch_settings.preservation_bias < 0.5) {
    score.tonal_balance -= 0.10;
  }

  // Low end stability
  score.low_end_stability = 0.90;
  if (evidence.has_kick_bass_masking && candidate.branch_settings.preservation_bias < 0.4) {
    score.low_end_stability -= 0.15;
  }

  // Punch & transient preservation
  if (candidate.branch_settings.preservation_bias > 0.6) {
    score.punch_and_transients = 0.92;
  } else {
    score.punch_and_transients = 0.82;
  }

  // Loudness density
  score.loudness_density = 0.85;

  // Artifact risk
  score.artifact_risk = 0.05;
  if (candidate.branch_settings.target_lufs > constraints.target_lufs + 2.0) {
    score.artifact_risk += 0.20;
  }

  // Stereo integrity
  score.stereo_integrity = evidence.stereo_correlation;

  // Weighted overall composite score
  score.overall_score = 0.25 * score.tonal_balance +
                        0.25 * score.low_end_stability +
                        0.20 * score.punch_and_transients +
                        0.20 * score.loudness_density +
                        0.10 * (1.0 - score.artifact_risk);

  return score;
}

}  // namespace amt::decision
