#include "amt/revision/RevisionExplanation.h"

#include <sstream>

namespace amt::revision {

std::string RevisionExplanation::generate_explanation(
    const RevisionIntent& intent,
    const PlanEditResult& edit_result) {
  if (!edit_result.success) {
    return "Revision could not be applied: " + edit_result.error;
  }

  std::ostringstream ss;
  ss << "Revision based on '" << intent.original_prompt << "':\n";

  for (const auto& change : edit_result.applied_changes) {
    ss << " - " << change << "\n";
  }

  for (const auto& constraint : intent.constraints) {
    if (constraint.active) {
      ss << " - Obeyed constraint: " << constraint.rule << "\n";
    }
  }

  return ss.str();
}

}  // namespace amt::revision
