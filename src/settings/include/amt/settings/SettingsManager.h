#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace amt::settings {

enum class ExecutionProviderPreference {
  cpu,
  cuda,
  directml
};

struct AppSettings {
  int schema_version{1};
  std::string audio_output_device;
  int buffer_size_frames{512};
  int sample_rate_hz{44100};
  ExecutionProviderPreference execution_provider{ExecutionProviderPreference::cpu};
  std::filesystem::path models_directory;
  std::filesystem::path cache_directory;
  std::size_t max_cache_size_mb{2048};
  bool telemetry_enabled{false};
  bool crash_reports_enabled{false};
  std::string default_export_recipe{"studio_master"};
  bool high_dpi_scaling{true};
  bool dark_theme{true};
  std::string active_preference_profile{"default"};
};

class SettingsManager {
 public:
  explicit SettingsManager(std::filesystem::path settings_path = {});

  [[nodiscard]] const AppSettings& settings() const noexcept { return settings_; }
  [[nodiscard]] AppSettings& mutable_settings() noexcept { return settings_; }

  bool load(std::string& error);
  bool save(std::string& error) const;
  void reset_to_defaults();

  [[nodiscard]] static std::filesystem::path default_settings_path();

 private:
  std::filesystem::path path_;
  AppSettings settings_;
  mutable std::mutex mutex_;
};

}  // namespace amt::settings
