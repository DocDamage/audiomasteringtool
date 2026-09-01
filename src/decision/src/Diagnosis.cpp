#include "amt/decision/Diagnosis.h"

#include <algorithm>
#include <cmath>

namespace amt::decision {

TrackDiagnosis diagnose_track(const DecisionEvidence& evidence) {
  TrackDiagnosis diag{};

  diag.dynamic_headroom_db = std::max(0.0, -1.0 - evidence.true_peak_dbtp);

  if (evidence.integrated_lufs > -10.0) {
    diag.primary_genre_tendency = "loud_competitive";
  } else if (evidence.integrated_lufs < -16.0) {
    diag.primary_genre_tendency = "dynamic_acoustic";
  } else {
    diag.primary_genre_tendency = "modern_streaming";
  }

  if (evidence.stereo_correlation < 0.7) {
    diag.stereo_stability_score = 0.6;
    diag.issues.push_back("Wide low-end stereo spread creates phase cancellation risk in mono playback.");
  } else {
    diag.positive_attributes.push_back("Solid center stereo focus.");
  }

  if (evidence.has_kick_bass_masking) {
    diag.low_end_cleanliness_score = 0.65;
    diag.issues.push_back("Kick and bass compete in 50-80 Hz region, reducing punch.");
  } else {
    diag.positive_attributes.push_back("Clean low frequency balance.");
  }

  if (evidence.has_harsh_sibilance) {
    diag.high_end_smoothness_score = 0.70;
    diag.issues.push_back("Excessive harshness in 5-8 kHz high frequencies.");
  }

  return diag;
}

}  // namespace amt::decision
