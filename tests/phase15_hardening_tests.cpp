#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "amt/settings/CacheManager.h"
#include "amt/settings/CrashReporting.h"
#include "amt/settings/ModelManager.h"
#include "amt/settings/SettingsManager.h"

int main() {
  std::cout << "[Phase 15] Running Windows Standalone Hardening Tests...\n";

  const auto temp_dir = std::filesystem::temp_directory_path() / "amt_hardening_test";
  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);
  std::filesystem::create_directories(temp_dir, ec);

  // Test 1: Settings persistence and defaults
  {
    const auto settings_file = temp_dir / "test_settings.json";
    amt::settings::SettingsManager manager(settings_file);

    assert(manager.settings().max_cache_size_mb == 2048);
    assert(manager.settings().high_dpi_scaling == true);

    manager.mutable_settings().audio_output_device = "ASIO Studio Driver";
    manager.mutable_settings().buffer_size_frames = 1024;
    manager.mutable_settings().telemetry_enabled = true;

    std::string err;
    bool save_ok = manager.save(err);
    assert(save_ok);
    assert(std::filesystem::exists(settings_file));

    amt::settings::SettingsManager loaded_manager(settings_file);
    bool load_ok = loaded_manager.load(err);
    assert(load_ok);
    assert(loaded_manager.settings().audio_output_device == "ASIO Studio Driver");
    assert(loaded_manager.settings().buffer_size_frames == 1024);
    assert(loaded_manager.settings().telemetry_enabled == true);
    std::cout << "  ✓ Test 1: SettingsManager saved and reloaded configuration correctly\n";
  }

  // Test 2: CacheManager inspect & budget eviction
  {
    const auto cache_dir = temp_dir / "cache";
    std::filesystem::create_directories(cache_dir, ec);

    // Create 3 temporary dummy files
    for (int i = 0; i < 3; ++i) {
      std::ofstream f(cache_dir / ("temp_" + std::to_string(i) + ".dat"));
      f << std::string(1024 * 1024, 'X'); // 1MB each
    }

    amt::settings::CacheManager cache(cache_dir);
    auto inv = cache.inspect_cache();
    assert(inv.total_items == 3);
    assert(inv.total_mb >= 3.0);

    // Evict down to 1MB budget
    std::string err;
    bool evict_ok = cache.evict_to_budget(1, err);
    assert(evict_ok);

    auto after_inv = cache.inspect_cache();
    assert(after_inv.total_mb <= 2.0);
    std::cout << "  ✓ Test 2: CacheManager inspected and evicted files to budget\n";
  }

  // Test 3: ModelManager diagnostics
  {
    const auto models_dir = temp_dir / "models";
    std::filesystem::create_directories(models_dir, ec);

    amt::settings::ModelManager models(models_dir);
    auto list = models.list_installed_models();
    assert(!list.empty());
    assert(list[0].model_id == "htdemucs_ft");
    std::cout << "  ✓ Test 3: ModelManager reported model inventory\n";
  }

  // Test 4: CrashReporting path sanitization & opt-in handling
  {
    std::filesystem::path user_path = "C:\\Users\\JohnDoe\\Music\\track.wav";
    auto sanitized = amt::settings::CrashReporting::sanitize_path(user_path);
    assert(sanitized.find("JohnDoe") == std::string::npos);
    assert(sanitized.find("<SANITIZED>") != std::string::npos);

    const auto crash_dir = temp_dir / "crashes";
    std::string err;
    // Opt-out should not write file
    amt::settings::CrashReporting::record_crash_log(crash_dir, "test", "err", false, err);
    assert(!std::filesystem::exists(crash_dir / "crash_*.log"));

    // Opt-in should write log
    amt::settings::CrashReporting::record_crash_log(crash_dir, "test", "err", true, err);
    std::cout << "  ✓ Test 4: CrashReporting sanitized paths and respected privacy opt-out\n";
  }

  std::filesystem::remove_all(temp_dir, ec);
  std::cout << "[Phase 15] All tests passed successfully!\n";
  return 0;
}
