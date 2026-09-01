#pragma once

namespace amt::decision {

struct MasteringConstraints {
  double target_lufs{-14.0};
  double target_true_peak_dbtp{-1.0};
  double max_loudness_boost_db{8.0};
  double max_eq_cut_db{4.0};
  double max_eq_boost_db{3.0};
  double min_crest_factor_preserved_db{6.0};
  double max_distortion_risk{0.25};
};

}  // namespace amt::decision
