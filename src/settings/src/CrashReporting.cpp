#include "amt/settings/CrashReporting.h"
#include <chrono>
#include <fstream>
#include <system_error>

namespace amt::settings {

std::string CrashReporting::sanitize_path(const std::filesystem::path& path) {
  // Strip user directories (e.g. C:\Users\<name>\... -> <USER_DIR>\...)
  std::string s = path.string();
  auto pos = s.find("Users\\");
  if (pos != std::string::npos) {
    auto next_slash = s.find('\\', pos + 6);
    if (next_slash != std::string::npos) {
      s = s.substr(0, pos) + "Users\\<SANITIZED>" + s.substr(next_slash);
    }
  }
  return s;
}

bool CrashReporting::record_crash_log(
    const std::filesystem::path& log_dir,
    const std::string& context_info,
    const std::string& error_details,
    bool user_opted_in,
    std::string& error) {
  error.clear();
  if (!user_opted_in) {
    return true; // Opt-out respected
  }

  std::error_code ec;
  std::filesystem::create_directories(log_dir, ec);

  auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
  const auto log_file = log_dir / ("crash_" + std::to_string(ts) + ".log");

  std::ofstream out(log_file);
  if (!out.is_open()) {
    error = "Cannot create crash log file: " + log_file.string();
    return false;
  }

  out << "Timestamp: " << ts << "\n"
      << "Context: " << context_info << "\n"
      << "Error: " << error_details << "\n";

  return true;
}

}  // namespace amt::settings
