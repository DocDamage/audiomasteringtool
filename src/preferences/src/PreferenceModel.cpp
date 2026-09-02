#include "amt/preferences/PreferenceModel.h"
#include <algorithm>
#include <cmath>

namespace amt::preferences {

PreferenceVector PreferenceModel::compute_preference_vector(
    const std::string& profile_name,
    const std::vector<PreferenceEvent>& events) {
  PreferenceVector vec;
  vec.profile_name = profile_name;
  vec.event_count = events.size();

  if (events.empty()) {
    return vec;
  }

  double sum_lufs = 0.0;
  double sum_bright = 0.0;
  double sum_bass = 0.0;
  double sum_width = 0.0;
  double sum_sat = 0.0;
  double sum_punch = 0.0;
  double total_weight = 0.0;

  // Weight more recent events more heavily
  double weight = 1.0;
  for (const auto& ev : events) {
    sum_lufs += ev.delta_lufs * weight;
    sum_bright += ev.delta_brightness * weight;
    sum_bass += ev.delta_bass * weight;
    sum_width += ev.delta_width * weight;
    sum_sat += ev.delta_saturation * weight;
    sum_punch += ev.delta_punch * weight;
    total_weight += weight;
    weight *= 1.02; // slight recency bias
  }

  if (total_weight > 0.0) {
    // Apply conservative learning rate scale (e.g. 0.25) so preferences don't overshoot
    double scale = 0.25 / total_weight;
    vec.loudness_bias_lu = std::clamp(sum_lufs * scale, -2.0, 2.0);
    vec.brightness_bias_db = std::clamp(sum_bright * scale, -3.0, 3.0);
    vec.bass_bias_db = std::clamp(sum_bass * scale, -3.0, 3.0);
    vec.stereo_width_scale = std::clamp(1.0 + (sum_width * scale), 0.80, 1.25);
    vec.saturation_bias = std::clamp(sum_sat * scale, -0.5, 0.5);
    vec.punch_bias = std::clamp(sum_punch * scale, -0.5, 0.5);
  }

  return vec;
}

}  // namespace amt::preferences
