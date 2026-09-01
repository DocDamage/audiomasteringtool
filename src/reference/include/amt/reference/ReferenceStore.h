#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/reference/ReferenceProfile.h"

namespace amt::reference {

struct MySoundProfile {
  int schema_version{1};
  std::string profile_name{"My Sound"};
  int reference_count{0};
  int user_selections_count{0};
  ReferenceProfile aggregate_profile;
};

class ReferenceStore {
 public:
  explicit ReferenceStore(std::filesystem::path root_directory = {});
  ~ReferenceStore() = default;

  [[nodiscard]] bool save_profile(
      const ReferenceProfile& profile,
      std::string& error) const;

  [[nodiscard]] std::optional<ReferenceProfile> load_profile(
      const std::filesystem::path& profile_path,
      std::string& error) const;

  [[nodiscard]] bool save_my_sound(
      const MySoundProfile& my_sound,
      std::string& error) const;

  [[nodiscard]] std::optional<MySoundProfile> load_my_sound(
      std::string& error) const;

  [[nodiscard]] MySoundProfile aggregate_profiles(
      const std::vector<ReferenceProfile>& profiles,
      const std::string& name = "My Sound") const;

 private:
  std::filesystem::path root_;
};

}  // namespace amt::reference
