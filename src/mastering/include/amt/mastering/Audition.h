#pragma once

#include "amt/analysis/LoudnessMeter.h"

namespace amt::mastering {

struct LoudnessMatchProfile {
  double reference_lufs{0.0};
  double original_gain_db{0.0};
  double master_a_gain_db{0.0};
  double master_b_gain_db{0.0};
};

[[nodiscard]] LoudnessMatchProfile make_loudness_match_profile(
    const amt::analysis::LoudnessMetrics& original,
    const amt::analysis::LoudnessMetrics& master_a,
    const amt::analysis::LoudnessMetrics& master_b);

}  // namespace amt::mastering
