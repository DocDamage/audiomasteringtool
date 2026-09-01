#pragma once

#include <string>
#include <vector>

#include "amt/decision/Evidence.h"

namespace amt::decision {

struct TrackDiagnosis {
  std::string primary_genre_tendency{"modern"};
  double dynamic_headroom_db{0.0};
  double low_end_cleanliness_score{1.0};
  double high_end_smoothness_score{1.0};
  double stereo_stability_score{1.0};
  std::vector<std::string> issues;
  std::vector<std::string> positive_attributes;
};

[[nodiscard]] TrackDiagnosis diagnose_track(const DecisionEvidence& evidence);

}  // namespace amt::decision
