#include "amt/settings/SettingsManager.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace amt::settings {

std::filesystem::path SettingsManager::default_settings_path() {
#ifdef _WIN32
  const char* local_app_data = std::getenv("LOCALAPPDATA");
  if (local_app_data && *local_app_data) {
    return std::filesystem::path(local_app_data) / "AudioMasteringTool" / "settings.json";
  }
#endif
  return std::filesystem::temp_directory_path() / "AudioMasteringTool" / "settings.json";
}

SettingsManager::SettingsManager(std::filesystem::path settings_path)
    : path_(settings_path.empty() ? default_settings_path() : std::move(settings_path)) {
  reset_to_defaults();
}

void SettingsManager::reset_to_defaults() {
  std::scoped_lock lock(mutex_);
  settings_ = AppSettings{};
#ifdef _WIN32
  const char* local_app_data = std::getenv("LOCALAPPDATA");
  if (local_app_data && *local_app_data) {
    auto base = std::filesystem::path(local_app_data) / "AudioMasteringTool";
    settings_.models_directory = base / "models";
    settings_.cache_directory = base / "cache";
  }
#endif
}

bool SettingsManager::save(std::string& error) const {
  error.clear();
  std::scoped_lock lock(mutex_);

  std::error_code ec;
  std::filesystem::create_directories(path_.parent_path(), ec);

  std::ofstream out(path_);
  if (!out.is_open()) {
    error = "Could not open settings file for write: " + path_.string();
    return false;
  }

  out << "{\n"
      << "  \"schema_version\": " << settings_.schema_version << ",\n"
      << "  \"audio_output_device\": \"" << settings_.audio_output_device << "\",\n"
      << "  \"buffer_size_frames\": " << settings_.buffer_size_frames << ",\n"
      << "  \"sample_rate_hz\": " << settings_.sample_rate_hz << ",\n"
      << "  \"execution_provider\": " << static_cast<int>(settings_.execution_provider) << ",\n"
      << "  \"models_directory\": \"" << settings_.models_directory.string() << "\",\n"
      << "  \"cache_directory\": \"" << settings_.cache_directory.string() << "\",\n"
      << "  \"max_cache_size_mb\": " << settings_.max_cache_size_mb << ",\n"
      << "  \"telemetry_enabled\": " << (settings_.telemetry_enabled ? "true" : "false") << ",\n"
      << "  \"crash_reports_enabled\": " << (settings_.crash_reports_enabled ? "true" : "false") << ",\n"
      << "  \"default_export_recipe\": \"" << settings_.default_export_recipe << "\",\n"
      << "  \"high_dpi_scaling\": " << (settings_.high_dpi_scaling ? "true" : "false") << ",\n"
      << "  \"dark_theme\": " << (settings_.dark_theme ? "true" : "false") << ",\n"
      << "  \"active_preference_profile\": \"" << settings_.active_preference_profile << "\"\n"
      << "}\n";

  return true;
}

bool SettingsManager::load(std::string& error) {
  error.clear();
  std::scoped_lock lock(mutex_);

  if (!std::filesystem::exists(path_)) {
    // If no file exists yet, return default settings cleanly
    return true;
  }

  std::ifstream in(path_);
  if (!in.is_open()) {
    error = "Could not open settings file: " + path_.string();
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  auto get_str = [&](const std::string& key) -> std::string {
    auto pos = content.find("\"" + key + "\": \"");
    if (pos != std::string::npos) {
      auto start = pos + key.size() + 4;
      auto end = content.find("\"", start);
      if (end != std::string::npos) {
        return content.substr(start, end - start);
      }
    }
    return "";
  };

  auto get_bool = [&](const std::string& key, bool default_val) -> bool {
    auto pos = content.find("\"" + key + "\":");
    if (pos != std::string::npos) {
      if (content.find("true", pos) != std::string::npos &&
          content.find("true", pos) < content.find("\n", pos)) {
        return true;
      }
      if (content.find("false", pos) != std::string::npos &&
          content.find("false", pos) < content.find("\n", pos)) {
        return false;
      }
    }
    return default_val;
  };

  auto get_int = [&](const std::string& key, int default_val) -> int {
    auto pos = content.find("\"" + key + "\":");
    if (pos != std::string::npos) {
      try {
        return std::stoi(content.substr(pos + key.size() + 2));
      } catch (...) {}
    }
    return default_val;
  };

  settings_.audio_output_device = get_str("audio_output_device");
  settings_.default_export_recipe = get_str("default_export_recipe");
  if (settings_.default_export_recipe.empty()) settings_.default_export_recipe = "studio_master";
  settings_.active_preference_profile = get_str("active_preference_profile");
  if (settings_.active_preference_profile.empty()) settings_.active_preference_profile = "default";

  settings_.buffer_size_frames = get_int("buffer_size_frames", 512);
  settings_.sample_rate_hz = get_int("sample_rate_hz", 44100);
  settings_.max_cache_size_mb = static_cast<std::size_t>(get_int("max_cache_size_mb", 2048));

  settings_.telemetry_enabled = get_bool("telemetry_enabled", false);
  settings_.crash_reports_enabled = get_bool("crash_reports_enabled", false);
  settings_.high_dpi_scaling = get_bool("high_dpi_scaling", true);
  settings_.dark_theme = get_bool("dark_theme", true);

  return true;
}

}  // namespace amt::settings
