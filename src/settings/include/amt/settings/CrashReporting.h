#pragma once

#include <filesystem>
#include <string>

namespace amt::settings {

class CrashReporting {
 public:
  [[nodiscard]] static std::string sanitize_path(const std::filesystem::path& path);

  static bool record_crash_log(
      const std::filesystem::path& log_dir,
      const std::string& context_info,
      const std::string& error_details,
      bool user_opted_in,
      std::string& error);
};

}  // namespace amt::settings
