#include "amt/mastering/Audition.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace amt::mastering {
namespace {

double safe_lufs(const double value) {
  return std::isfinite(value) && value > -80.0 ? value : -80.0;
}

}  // namespace

LoudnessMatchProfile make_loudness_match_profile(
    const amt::analysis::LoudnessMetrics& original,
    const amt::analysis::LoudnessMetrics& master_a,
    const amt::analysis::LoudnessMetrics& master_b) {
  const std::array<double, 3> values = {
      safe_lufs(original.integrated_lufs), safe_lufs(master_a.integrated_lufs),
      safe_lufs(master_b.integrated_lufs)};
  const double reference = *std::min_element(values.begin(), values.end());
  return {.reference_lufs = reference,
          .original_gain_db = reference - values[0],
          .master_a_gain_db = reference - values[1],
          .master_b_gain_db = reference - values[2]};
}

}  // namespace amt::mastering
