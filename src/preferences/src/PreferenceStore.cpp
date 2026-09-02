#include "amt/preferences/PreferenceStore.h"
#include <fstream>
#include <sstream>
#include <system_error>

namespace amt::preferences {

PreferenceStore::PreferenceStore(std::filesystem::path root_directory)
    : root_(std::move(root_directory)) {
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
}

bool PreferenceStore::record_event(const PreferenceEvent& event, std::string& error) {
  error.clear();
  if (!learning_enabled_) {
    return true; // silently ignored if learning disabled
  }

  std::scoped_lock lock(mutex_);
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);

  const auto file_path = root_ / ("events_" + event.profile_name + ".jsonl");
  std::ofstream out(file_path, std::ios::app);
  if (!out.is_open()) {
    error = "Could not open preference event file: " + file_path.string();
    return false;
  }

  out << "{\"event_id\":\"" << event.event_id << "\""
      << ",\"timestamp_ms\":" << event.timestamp_ms
      << ",\"profile_name\":\"" << event.profile_name << "\""
      << ",\"event_type\":" << static_cast<int>(event.event_type)
      << ",\"delta_lufs\":" << event.delta_lufs
      << ",\"delta_brightness\":" << event.delta_brightness
      << ",\"delta_bass\":" << event.delta_bass
      << ",\"delta_width\":" << event.delta_width
      << ",\"delta_saturation\":" << event.delta_saturation
      << ",\"delta_punch\":" << event.delta_punch
      << ",\"notes\":\"" << event.notes << "\"}\n";

  return true;
}

std::vector<PreferenceEvent> PreferenceStore::load_events(
    const std::string& profile_name, std::string& error) const {
  error.clear();
  std::scoped_lock lock(mutex_);
  std::vector<PreferenceEvent> events;

  const auto file_path = root_ / ("events_" + profile_name + ".jsonl");
  if (!std::filesystem::exists(file_path)) {
    return events;
  }

  std::ifstream in(file_path);
  if (!in.is_open()) {
    error = "Could not open preference events file";
    return events;
  }

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    PreferenceEvent ev;
    ev.profile_name = profile_name;
    // Simple fast parse of key values
    auto get_double = [&](const std::string& key) -> double {
      auto pos = line.find("\"" + key + "\":");
      if (pos != std::string::npos) {
        return std::stod(line.substr(pos + key.size() + 3));
      }
      return 0.0;
    };

    ev.delta_lufs = get_double("delta_lufs");
    ev.delta_brightness = get_double("delta_brightness");
    ev.delta_bass = get_double("delta_bass");
    ev.delta_width = get_double("delta_width");
    ev.delta_saturation = get_double("delta_saturation");
    ev.delta_punch = get_double("delta_punch");
    events.push_back(ev);
  }

  return events;
}

bool PreferenceStore::reset_profile(const std::string& profile_name, std::string& error) {
  error.clear();
  std::scoped_lock lock(mutex_);
  const auto file_path = root_ / ("events_" + profile_name + ".jsonl");
  std::error_code ec;
  if (std::filesystem::exists(file_path)) {
    std::filesystem::remove(file_path, ec);
    if (ec) {
      error = "Failed to remove preference file: " + ec.message();
      return false;
    }
  }
  return true;
}

bool PreferenceStore::export_all(const std::filesystem::path& destination_file, std::string& error) const {
  error.clear();
  std::scoped_lock lock(mutex_);
  std::ofstream out(destination_file);
  if (!out.is_open()) {
    error = "Cannot open export destination: " + destination_file.string();
    return false;
  }

  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
      std::ifstream in(entry.path());
      out << in.rdbuf();
    }
  }
  return true;
}

bool PreferenceStore::import_all(const std::filesystem::path& source_file, std::string& error) {
  error.clear();
  if (!std::filesystem::exists(source_file)) {
    error = "Source import file does not exist: " + source_file.string();
    return false;
  }

  std::scoped_lock lock(mutex_);
  std::ifstream in(source_file);
  if (!in.is_open()) {
    error = "Cannot open import file: " + source_file.string();
    return false;
  }

  const auto dest = root_ / "events_default.jsonl";
  std::ofstream out(dest, std::ios::app);
  out << in.rdbuf();
  return true;
}

}  // namespace amt::preferences
