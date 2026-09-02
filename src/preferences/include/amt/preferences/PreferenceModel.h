#pragma once

#include <vector>
#include "amt/preferences/PreferenceEvent.h"
#include "amt/preferences/PreferenceVector.h"

namespace amt::preferences {

class PreferenceModel {
 public:
  [[nodiscard]] static PreferenceVector compute_preference_vector(
      const std::string& profile_name,
      const std::vector<PreferenceEvent>& events);
};

}  // namespace amt::preferences
