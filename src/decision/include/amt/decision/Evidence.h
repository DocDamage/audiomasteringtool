#pragma once

#include <string>
#include <vector>

#include "amt/core/AnalysisTypes.h"

namespace amt::decision {

struct DecisionEvidence {
  double integrated_lufs{-70.0};
  double true_peak_dbtp{-70.0};
  double loudness_range_lu{0.0};
  double crest_factor_db{0.0};
  double spectral_centroid_hz{0.0};
  double sub_bass_ratio{0.0};
  double high_presence_ratio{0.0};
  double stereo_correlation{1.0};

  bool has_clipping{false};
  bool has_kick_bass_masking{false};
  bool has_harsh_sibilance{false};

  std::vector<std::string> detected_instruments;
  std::vector<std::string> key_insights;
};

}  // namespace amt::decision
