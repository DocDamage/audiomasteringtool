#include "amt/revision/ConstraintResolver.h"

namespace amt::revision {

ConstraintValidationResult ConstraintResolver::validate_constraints(
    const RevisionIntent& intent) {
  ConstraintValidationResult result;
  result.is_valid = true;

  for (const auto& constraint : intent.constraints) {
    if (!constraint.active) continue;

    for (const auto& action : intent.actions) {
      // Check direct collision: action targeting same scope that is constrained
      if (constraint.target == TargetScope::stem_bass &&
          (action.target == TargetScope::stem_bass || action.target == TargetScope::instrument_808) &&
          action.type == ActionType::adjust_bass_level) {
        result.is_valid = false;
        result.violated_constraints.push_back(
            "Action '" + action.raw_phrase + "' directly conflicts with constraint '" + constraint.rule + "'");
      }

      if (constraint.target == TargetScope::stem_vocal &&
          (action.target == TargetScope::stem_vocal || action.target == TargetScope::instrument_lead_vocal)) {
        result.is_valid = false;
        result.violated_constraints.push_back(
            "Action targeting vocals conflicts with constraint '" + constraint.rule + "'");
      }

      if (constraint.target == TargetScope::instrument_snare &&
          action.type == ActionType::adjust_loudness && action.amount > 0.8) {
        result.warnings.push_back(
            "Heavy limiting may compress snare transients; monitoring transient preservation");
      }
    }
  }

  return result;
}

}  // namespace amt::revision
