#include "amt/preferences/PreferenceVector.h"
#include <iomanip>
#include <sstream>

namespace amt::preferences {

std::string PreferenceVector::to_json() const {
  std::ostringstream ss;
  ss << "{\n"
     << "  \"profile_name\": \"" << profile_name << "\",\n"
     << "  \"event_count\": " << event_count << ",\n"
     << "  \"loudness_bias_lu\": " << loudness_bias_lu << ",\n"
     << "  \"brightness_bias_db\": " << brightness_bias_db << ",\n"
     << "  \"bass_bias_db\": " << bass_bias_db << ",\n"
     << "  \"stereo_width_scale\": " << stereo_width_scale << ",\n"
     << "  \"saturation_bias\": " << saturation_bias << ",\n"
     << "  \"punch_bias\": " << punch_bias << "\n"
     << "}";
  return ss.str();
}

PreferenceVector PreferenceVector::from_json(const std::string& /*json*/) {
  PreferenceVector vec;
  // Fallback defaults
  return vec;
}

}  // namespace amt::preferences
