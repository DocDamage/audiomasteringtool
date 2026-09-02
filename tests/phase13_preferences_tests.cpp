#include <cassert>
#include <filesystem>
#include <iostream>
#include "amt/preferences/PreferenceEvent.h"
#include "amt/preferences/PreferenceModel.h"
#include "amt/preferences/PreferenceStore.h"
#include "amt/preferences/PreferenceVector.h"
#include "amt/preferences/ProfilePreferences.h"

int main() {
  std::cout << "[Phase 13] Running Preference Learning Tests...\n";

  const auto temp_dir = std::filesystem::temp_directory_path() / "amt_pref_test";
  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);
  std::filesystem::create_directories(temp_dir, ec);

  auto store = std::make_shared<amt::preferences::PreferenceStore>(temp_dir);
  amt::preferences::ProfilePreferenceManager manager(store);

  // Test 1: Record events and calculate preference vector
  {
    std::string err;
    bool ok1 = manager.record_selection("electronic", true, err);
    assert(ok1);
    bool ok2 = manager.record_loudness_nudge("electronic", 1.0, err);
    assert(ok2);
    bool ok3 = manager.record_brightness_nudge("electronic", 1.5, err);
    assert(ok3);

    auto vec = manager.get_active_vector("electronic");
    assert(vec.profile_name == "electronic");
    assert(vec.event_count == 3);
    assert(vec.loudness_bias_lu > 0.0);
    assert(vec.brightness_bias_db > 0.0);
    assert(vec.loudness_bias_lu <= 2.0); // clamped within bounds
    std::cout << "  ✓ Test 1: Recorded preference events and computed bounded adaptation vector\n";
  }

  // Test 2: Multi-profile isolation
  {
    auto default_vec = manager.get_active_vector("default");
    assert(default_vec.event_count == 0);
    assert(default_vec.loudness_bias_lu == 0.0);

    auto elec_vec = manager.get_active_vector("electronic");
    assert(elec_vec.event_count == 3);
    std::cout << "  ✓ Test 2: Profile isolation verified across named profiles\n";
  }

  // Test 3: Export, Reset, and Import
  {
    const auto export_file = temp_dir / "exported_prefs.jsonl";
    std::string err;
    bool exp_ok = store->export_all(export_file, err);
    assert(exp_ok);
    assert(std::filesystem::exists(export_file));

    bool reset_ok = store->reset_profile("electronic", err);
    assert(reset_ok);

    auto after_reset = manager.get_active_vector("electronic");
    assert(after_reset.event_count == 0);

    bool imp_ok = store->import_all(export_file, err);
    assert(imp_ok);
    std::cout << "  ✓ Test 3: Export, reset, and import cycles verified\n";
  }

  // Test 4: Learning disable toggle
  {
    store->set_learning_enabled(false);
    assert(!store->is_learning_enabled());
    std::string err;
    manager.record_loudness_nudge("rock", 1.0, err);
    auto rock_vec = manager.get_active_vector("rock");
    assert(rock_vec.event_count == 0); // ignored when disabled
    std::cout << "  ✓ Test 4: Learning toggle obeys user privacy preference\n";
  }

  std::filesystem::remove_all(temp_dir, ec);
  std::cout << "[Phase 13] All tests passed successfully!\n";
  return 0;
}
