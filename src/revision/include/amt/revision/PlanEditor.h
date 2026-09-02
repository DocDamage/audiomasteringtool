#pragma once

#include <optional>
#include <string>
#include "amt/mastering/Planner.h"
#include "amt/revision/RevisionIntent.h"

namespace amt::revision {

struct PlanEditResult {
  bool success{false};
  amt::mastering::MasteringCandidatePlan revised_plan;
  std::vector<std::string> applied_changes;
  std::string error;
};

class PlanEditor {
 public:
  [[nodiscard]] static PlanEditResult apply_revision(
      const amt::mastering::MasteringCandidatePlan& base_plan,
      const RevisionIntent& intent);
};

}  // namespace amt::revision
