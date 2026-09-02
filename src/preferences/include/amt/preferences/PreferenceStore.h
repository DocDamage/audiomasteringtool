#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include "amt/preferences/PreferenceEvent.h"

namespace amt::preferences {

class PreferenceStore {
 public:
  explicit PreferenceStore(std::filesystem::path root_directory);

  [[nodiscard]] const std::filesystem::path& root_directory() const noexcept { return root_; }

  bool is_learning_enabled() const noexcept { return learning_enabled_; }
  void set_learning_enabled(bool enabled) noexcept { learning_enabled_ = enabled; }

  bool record_event(const PreferenceEvent& event, std::string& error);
  [[nodiscard]] std::vector<PreferenceEvent> load_events(const std::string& profile_name, std::string& error) const;
  bool reset_profile(const std::string& profile_name, std::string& error);

  bool export_all(const std::filesystem::path& destination_file, std::string& error) const;
  bool import_all(const std::filesystem::path& source_file, std::string& error);

 private:
  std::filesystem::path root_;
  bool learning_enabled_{true};
  mutable std::mutex mutex_;
};

}  // namespace amt::preferences
