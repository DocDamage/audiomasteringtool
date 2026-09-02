#pragma once

#include <string>
#include <vector>
#include <optional>

namespace amt::revision {

enum class ActionType {
  increase_punch,
  reduce_punch,
  increase_brightness,
  reduce_harshness,
  increase_warmth,
  reduce_mud,
  adjust_bass_level,
  adjust_stereo_width,
  adjust_saturation,
  adjust_loudness,
  preserve_transient,
  preserve_instrument,
  adjust_reference_influence,
  custom
};

enum class TargetScope {
  global,
  stem_drums,
  stem_bass,
  stem_vocal,
  stem_other,
  instrument_kick,
  instrument_808,
  instrument_snare,
  instrument_lead_vocal,
  section_hook,
  section_verse,
  time_range
};

struct NegativeConstraint {
  TargetScope target{TargetScope::global};
  std::string target_name;
  std::string rule; // e.g. "don't touch bass", "preserve snare", "leave vocals alone"
  bool active{true};
};

struct RevisionAction {
  ActionType type{ActionType::custom};
  TargetScope target{TargetScope::global};
  std::string target_name;
  double amount{0.5}; // Normalized strength [0.0, 1.0], default 0.5 (moderate)
  double numeric_delta_db{0.0}; // Optional explicit dB value if parsed
  std::string raw_phrase;
};

struct RevisionIntent {
  std::string original_prompt;
  std::vector<RevisionAction> actions;
  std::vector<NegativeConstraint> constraints;
  bool parsed_successfully{false};
  std::string parse_error;
};

}  // namespace amt::revision
