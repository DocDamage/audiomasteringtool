#include "amt/preferences/ProfilePreferences.h"
#include <chrono>

namespace amt::preferences {

namespace {

std::int64_t current_timestamp_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

ProfilePreferenceManager::ProfilePreferenceManager(std::shared_ptr<PreferenceStore> store)
    : store_(std::move(store)) {}

std::vector<std::string> ProfilePreferenceManager::list_profiles() const {
  std::vector<std::string> profiles = {"default", "hiphop", "electronic", "rock", "acoustic"};
  return profiles;
}

PreferenceVector ProfilePreferenceManager::get_active_vector(const std::string& profile_name) const {
  if (!store_) return PreferenceVector{.profile_name = profile_name};
  std::string err;
  auto events = store_->load_events(profile_name, err);
  return PreferenceModel::compute_preference_vector(profile_name, events);
}

bool ProfilePreferenceManager::record_selection(
    const std::string& profile_name, bool is_candidate_a, std::string& error) {
  if (!store_) return false;
  PreferenceEvent ev;
  ev.event_id = "sel_" + std::to_string(current_timestamp_ms());
  ev.timestamp_ms = current_timestamp_ms();
  ev.profile_name = profile_name;
  ev.event_type = is_candidate_a ? PreferenceEventType::candidate_a_selected
                                 : PreferenceEventType::candidate_b_selected;
  ev.delta_lufs = is_candidate_a ? 0.2 : -0.2;
  ev.notes = is_candidate_a ? "Selected Master A" : "Selected Master B";
  return store_->record_event(ev, error);
}

bool ProfilePreferenceManager::record_loudness_nudge(
    const std::string& profile_name, double delta_lu, std::string& error) {
  if (!store_) return false;
  PreferenceEvent ev;
  ev.event_id = "nudge_lufs_" + std::to_string(current_timestamp_ms());
  ev.timestamp_ms = current_timestamp_ms();
  ev.profile_name = profile_name;
  ev.event_type = PreferenceEventType::loudness_nudged;
  ev.delta_lufs = delta_lu;
  ev.notes = "Nudged target loudness";
  return store_->record_event(ev, error);
}

bool ProfilePreferenceManager::record_brightness_nudge(
    const std::string& profile_name, double delta_db, std::string& error) {
  if (!store_) return false;
  PreferenceEvent ev;
  ev.event_id = "nudge_bright_" + std::to_string(current_timestamp_ms());
  ev.timestamp_ms = current_timestamp_ms();
  ev.profile_name = profile_name;
  ev.event_type = PreferenceEventType::revision_accepted;
  ev.delta_brightness = delta_db;
  ev.notes = "Nudged high-frequency balance";
  return store_->record_event(ev, error);
}

bool ProfilePreferenceManager::record_bass_nudge(
    const std::string& profile_name, double delta_db, std::string& error) {
  if (!store_) return false;
  PreferenceEvent ev;
  ev.event_id = "nudge_bass_" + std::to_string(current_timestamp_ms());
  ev.timestamp_ms = current_timestamp_ms();
  ev.profile_name = profile_name;
  ev.event_type = PreferenceEventType::revision_accepted;
  ev.delta_bass = delta_db;
  ev.notes = "Nudged low-end balance";
  return store_->record_event(ev, error);
}

}  // namespace amt::preferences
