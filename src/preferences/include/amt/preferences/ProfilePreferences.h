#pragma once

#include <memory>
#include <string>
#include <vector>
#include "amt/preferences/PreferenceModel.h"
#include "amt/preferences/PreferenceStore.h"

namespace amt::preferences {

class ProfilePreferenceManager {
 public:
  explicit ProfilePreferenceManager(std::shared_ptr<PreferenceStore> store);

  [[nodiscard]] std::vector<std::string> list_profiles() const;
  [[nodiscard]] PreferenceVector get_active_vector(const std::string& profile_name) const;

  bool record_selection(const std::string& profile_name, bool is_candidate_a, std::string& error);
  bool record_loudness_nudge(const std::string& profile_name, double delta_lu, std::string& error);
  bool record_brightness_nudge(const std::string& profile_name, double delta_db, std::string& error);
  bool record_bass_nudge(const std::string& profile_name, double delta_db, std::string& error);

 private:
  std::shared_ptr<PreferenceStore> store_;
};

}  // namespace amt::preferences
