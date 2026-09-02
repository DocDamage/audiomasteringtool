#pragma once

#include <cstddef>
#include <string>

namespace amt::preferences {

struct PreferenceVector {
  std::string profile_name{"default"};
  std::size_t event_count{0};

  // Strictly bounded continuous preference biases
  double loudness_bias_lu{0.0};       // [-2.0, +2.0]
  double brightness_bias_db{0.0};     // [-3.0, +3.0]
  double bass_bias_db{0.0};           // [-3.0, +3.0]
  double stereo_width_scale{1.0};     // [0.8, 1.25]
  double saturation_bias{0.0};        // [-0.5, +0.5]
  double punch_bias{0.0};             // [-0.5, +0.5]

  [[nodiscard]] std::string to_json() const;
  static PreferenceVector from_json(const std::string& json);
};

}  // namespace amt::preferences
