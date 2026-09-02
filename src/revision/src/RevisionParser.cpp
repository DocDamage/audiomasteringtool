#include "amt/revision/RevisionParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace amt::revision {

namespace {

std::string to_lower(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (char ch : text) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return result;
}

bool contains_phrase(const std::string& text, const std::string& phrase) {
  return text.find(phrase) != std::string::npos;
}

}  // namespace

RevisionParser::RevisionParser(std::shared_ptr<ILanguageModelProvider> llm_provider)
    : llm_provider_(std::move(llm_provider)) {}

RevisionIntent RevisionParser::parse(const std::string& prompt) const {
  if (prompt.empty()) {
    RevisionIntent intent;
    intent.original_prompt = prompt;
    intent.parsed_successfully = false;
    intent.parse_error = "Empty prompt";
    return intent;
  }

  // If external LLM provider is available, attempt complex parse first
  if (llm_provider_ && llm_provider_->is_available()) {
    std::string err;
    if (auto llm_result = llm_provider_->parse_prompt(prompt, err)) {
      if (llm_result->parsed_successfully) {
        return *llm_result;
      }
    }
  }

  return parse_deterministic(prompt);
}

RevisionIntent RevisionParser::parse_deterministic(const std::string& prompt) const {
  RevisionIntent intent;
  intent.original_prompt = prompt;
  std::string lower = to_lower(prompt);

  // Extract negative constraints first
  if (contains_phrase(lower, "don't touch the bass") || contains_phrase(lower, "dont touch the bass") ||
      contains_phrase(lower, "don't touch bass") || contains_phrase(lower, "leave the bass alone") ||
      contains_phrase(lower, "leave bass alone") || contains_phrase(lower, "preserve the bass") ||
      contains_phrase(lower, "preserve bass")) {
    intent.constraints.push_back({TargetScope::stem_bass, "bass", "don't touch bass", true});
  }

  if (contains_phrase(lower, "don't touch vocals") || contains_phrase(lower, "leave vocals alone") ||
      contains_phrase(lower, "preserve vocals") || contains_phrase(lower, "leave the vocal alone") ||
      contains_phrase(lower, "don't touch the vocal") || contains_phrase(lower, "without touching vocals")) {
    intent.constraints.push_back({TargetScope::stem_vocal, "vocal", "leave vocals alone", true});
  }

  if (contains_phrase(lower, "without flattening the snare") || contains_phrase(lower, "preserve the snare") ||
      contains_phrase(lower, "preserve snare") || contains_phrase(lower, "keep snare punch") ||
      contains_phrase(lower, "don't squash the snare")) {
    intent.constraints.push_back({TargetScope::instrument_snare, "snare", "preserve snare transient", true});
  }

  if (contains_phrase(lower, "only in the hook") || contains_phrase(lower, "only in hook") ||
      contains_phrase(lower, "only the hook") || contains_phrase(lower, "only chorus")) {
    intent.constraints.push_back({TargetScope::section_hook, "hook", "only in hook section", true});
  }

  // Parse positive actions
  // 1. Punch / Transient
  if (contains_phrase(lower, "hit harder") || contains_phrase(lower, "more punch") ||
      contains_phrase(lower, "punchier") || contains_phrase(lower, "tighter drums") ||
      contains_phrase(lower, "punchy drums") || contains_phrase(lower, "boost punch")) {
    RevisionAction action;
    action.type = ActionType::increase_punch;
    action.target = TargetScope::stem_drums;
    action.target_name = "drums";
    action.amount = 0.6;
    action.raw_phrase = "increase drum punch";
    intent.actions.push_back(action);
  } else if (contains_phrase(lower, "less punch") || contains_phrase(lower, "softer transient") ||
             contains_phrase(lower, "tame drums")) {
    RevisionAction action;
    action.type = ActionType::reduce_punch;
    action.target = TargetScope::stem_drums;
    action.target_name = "drums";
    action.amount = 0.5;
    action.raw_phrase = "reduce drum punch";
    intent.actions.push_back(action);
  }

  // 2. Harshness / Highs / Air
  if (contains_phrase(lower, "hats are too sharp") || contains_phrase(lower, "too sharp") ||
      contains_phrase(lower, "too harsh") || contains_phrase(lower, "tame harshness") ||
      contains_phrase(lower, "reduce harshness") || contains_phrase(lower, "harsh") ||
      contains_phrase(lower, "smooth the top") || contains_phrase(lower, "less harsh") ||
      contains_phrase(lower, "tame hats")) {
    RevisionAction action;
    action.type = ActionType::reduce_harshness;
    action.target = TargetScope::global;
    action.target_name = "highs";
    action.amount = 0.6;
    action.raw_phrase = "reduce harshness in upper frequencies";
    intent.actions.push_back(action);
  } else if (contains_phrase(lower, "more air") || contains_phrase(lower, "brighter") ||
             contains_phrase(lower, "more brightness") || contains_phrase(lower, "more top end") ||
             contains_phrase(lower, "add air") || contains_phrase(lower, "crisper")) {
    RevisionAction action;
    action.type = ActionType::increase_brightness;
    action.target = TargetScope::global;
    action.target_name = "air";
    action.amount = 0.5;
    action.raw_phrase = "increase high-end air/brightness";
    intent.actions.push_back(action);
  }

  // 3. Bass / 808 / Low end
  if (contains_phrase(lower, "back the 808 down") || contains_phrase(lower, "turn down 808") ||
      contains_phrase(lower, "less 808") || contains_phrase(lower, "lower 808") ||
      contains_phrase(lower, "808 is too loud")) {
    RevisionAction action;
    action.type = ActionType::adjust_bass_level;
    action.target = TargetScope::instrument_808;
    action.target_name = "808";
    action.amount = -0.5;
    action.numeric_delta_db = -1.5;
    action.raw_phrase = "reduce 808 level";
    intent.actions.push_back(action);
  } else if (contains_phrase(lower, "more bass") || contains_phrase(lower, "more sub") ||
             contains_phrase(lower, "boost bass") || contains_phrase(lower, "heavier bass") ||
             contains_phrase(lower, "fatter low end")) {
    RevisionAction action;
    action.type = ActionType::adjust_bass_level;
    action.target = TargetScope::stem_bass;
    action.target_name = "bass";
    action.amount = 0.5;
    action.numeric_delta_db = 1.5;
    action.raw_phrase = "increase bass level";
    intent.actions.push_back(action);
  } else if (contains_phrase(lower, "less bass") || contains_phrase(lower, "too much bass") ||
             contains_phrase(lower, "reduce sub") || contains_phrase(lower, "less sub")) {
    RevisionAction action;
    action.type = ActionType::adjust_bass_level;
    action.target = TargetScope::stem_bass;
    action.target_name = "bass";
    action.amount = -0.5;
    action.numeric_delta_db = -1.5;
    action.raw_phrase = "reduce bass level";
    intent.actions.push_back(action);
  }

  // 4. Mud / Warmth
  if (contains_phrase(lower, "reduce mud") || contains_phrase(lower, "less muddy") ||
      contains_phrase(lower, "muddy") || contains_phrase(lower, "clean up the low mids") ||
      contains_phrase(lower, "too boxy") || contains_phrase(lower, "boxy")) {
    RevisionAction action;
    action.type = ActionType::reduce_mud;
    action.target = TargetScope::global;
    action.target_name = "low_mids";
    action.amount = 0.5;
    action.raw_phrase = "reduce low-mid mud";
    intent.actions.push_back(action);
  } else if (contains_phrase(lower, "more warmth") || contains_phrase(lower, "warmer") ||
             contains_phrase(lower, "add warmth")) {
    RevisionAction action;
    action.type = ActionType::increase_warmth;
    action.target = TargetScope::global;
    action.target_name = "warmth";
    action.amount = 0.5;
    action.raw_phrase = "increase low-mid warmth";
    intent.actions.push_back(action);
  }

  // 5. Width / Stereo
  if (contains_phrase(lower, "more width") || contains_phrase(lower, "wider") ||
      contains_phrase(lower, "increase width") || contains_phrase(lower, "spread it out")) {
    RevisionAction action;
    action.type = ActionType::adjust_stereo_width;
    action.target = TargetScope::global;
    action.target_name = "stereo_field";
    action.amount = 0.6;
    action.raw_phrase = "increase stereo width";
    intent.actions.push_back(action);
  } else if (contains_phrase(lower, "narrower") || contains_phrase(lower, "less width") ||
             contains_phrase(lower, "tighter stereo") || contains_phrase(lower, "more centered")) {
    RevisionAction action;
    action.type = ActionType::adjust_stereo_width;
    action.target = TargetScope::global;
    action.target_name = "stereo_field";
    action.amount = -0.4;
    action.raw_phrase = "narrow stereo width";
    intent.actions.push_back(action);
  }

  // 6. Loudness / Level
  if (contains_phrase(lower, "make it louder") || contains_phrase(lower, "louder") ||
      contains_phrase(lower, "push it harder") || contains_phrase(lower, "more level") ||
      contains_phrase(lower, "hotter master")) {
    RevisionAction action;
    action.type = ActionType::adjust_loudness;
    action.target = TargetScope::global;
    action.target_name = "master";
    action.amount = 0.6;
    action.numeric_delta_db = 1.0;
    action.raw_phrase = "increase master target loudness";
    intent.actions.push_back(action);
  } else if (contains_phrase(lower, "more dynamic") || contains_phrase(lower, "more headroom") ||
             contains_phrase(lower, "quieter") || contains_phrase(lower, "less squashed")) {
    RevisionAction action;
    action.type = ActionType::adjust_loudness;
    action.target = TargetScope::global;
    action.target_name = "master";
    action.amount = -0.5;
    action.numeric_delta_db = -1.0;
    action.raw_phrase = "preserve dynamics / reduce target loudness";
    intent.actions.push_back(action);
  }

  // 7. Saturation / Grit
  if (contains_phrase(lower, "more grit") || contains_phrase(lower, "add saturation") ||
      contains_phrase(lower, "more color") || contains_phrase(lower, "analog feel") ||
      contains_phrase(lower, "tape warmth") || contains_phrase(lower, "more bite")) {
    RevisionAction action;
    action.type = ActionType::adjust_saturation;
    action.target = TargetScope::global;
    action.target_name = "saturation";
    action.amount = 0.5;
    action.raw_phrase = "increase harmonic saturation";
    intent.actions.push_back(action);
  }

  // Fallback / Custom intent if no standard pattern matched
  if (intent.actions.empty() && intent.constraints.empty()) {
    RevisionAction custom_action;
    custom_action.type = ActionType::custom;
    custom_action.target = TargetScope::global;
    custom_action.target_name = "custom";
    custom_action.amount = 0.5;
    custom_action.raw_phrase = prompt;
    intent.actions.push_back(custom_action);
  }

  intent.parsed_successfully = true;
  return intent;
}

}  // namespace amt::revision
