#pragma once

#include <array>
#include <string>
#include <vector>

namespace amt::reference {

struct SpectralBands {
  double sub_db{-30.0};       // 20-60 Hz
  double bass_db{-20.0};      // 60-250 Hz
  double low_mid_db{-18.0};   // 250-500 Hz
  double mid_db{-15.0};       // 500-2000 Hz
  double high_mid_db{-18.0};  // 2000-6000 Hz
  double presence_db{-22.0};  // 6000-12000 Hz
  double air_db{-28.0};       // 12000-20000 Hz
};

struct ReferenceProfile {
  int schema_version{1};
  std::string profile_id;
  std::string display_name;
  std::string source_track_name;
  std::string genre_tag;

  double integrated_lufs{-14.0};
  double true_peak_dbtp{-1.0};
  double loudness_range_lu{6.0};
  double crest_factor_db{10.0};
  double stereo_width{1.0};

  SpectralBands spectrum;
  std::vector<double> low_frequency_curve;
};

}  // namespace amt::reference
