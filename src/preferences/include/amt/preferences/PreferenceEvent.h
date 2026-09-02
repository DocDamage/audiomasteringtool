#pragma once

#include <cstdint>
#include <string>

namespace amt::preferences {

enum class PreferenceEventType {
  candidate_a_selected,
  candidate_b_selected,
  revision_accepted,
  revision_rejected,
  loudness_nudged,
  reference_profile_applied,
  style_override
};

struct PreferenceEvent {
  std::string event_id;
  std::int64_t timestamp_ms{0};
  std::string profile_name{"default"};
  PreferenceEventType event_type{PreferenceEventType::candidate_a_selected};
  double delta_lufs{0.0};
  double delta_brightness{0.0};
  double delta_bass{0.0};
  double delta_width{0.0};
  double delta_saturation{0.0};
  double delta_punch{0.0};
  std::string notes;
};

}  // namespace amt::preferences
