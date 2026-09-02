#include "amt/settings/CrashReporting.h"
#include <chrono>
#include <fstream>
#include <system_error>

namespace amt::settings {

std::string CrashReporting::sanitize_path(const std::filesystem::path& path) {
  // Strip user directories (e.g. C:\Users\<name>\... -> <USER_DIR>\... or /home/<name>/...)
  std::string s = path.string();

  // Windows backslash pattern
  auto pos = s.find("Users\\");
  if (pos != std::string::npos) {
    auto next_slash = s.find('\\', pos + 6);
    if (next_slash != std::string::npos) {
      s = s.substr(0, pos) + "Users\\<SANITIZED>" + s.substr(next_slash);
    }
  }

  // Windows / Unix forward slash pattern
  auto pos_fwd = s.find("Users/");
  if (pos_fwd != std::string::npos) {
    auto next_slash = s.find('/', pos_fwd + 6);
    if (next_slash != std::string::npos) {
      s = s.substr(0, pos_fwd) + "Users/<SANITIZED>" + s.substr(next_slash);
    }
  }

  auto pos_home = s.find("/home/");
  if (pos_home != std::string::npos) {
    auto next_slash = s.find('/', pos_home + 6);
    if (next_slash != std::string::npos) {
      s = s.substr(0, pos_home) + "/home/<SANITIZED>" + s.substr(next_slash);
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
  if (ec) {
    error = "Cannot create crash directory: " + ec.message();
    return false;
  }

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
      << "Context: " << sanitize_path(context_info) << "\n"
      << "Error: " << sanitize_path(error_details) << "\n";

  return true;
}

}  // namespace amt::settings
