#pragma once

#include <string>
#include <vector>
#include "amt/revision/PlanEditor.h"
#include "amt/revision/RevisionIntent.h"

namespace amt::revision {

class RevisionExplanation {
 public:
  [[nodiscard]] static std::string generate_explanation(
      const RevisionIntent& intent,
      const PlanEditResult& edit_result);
};

}  // namespace amt::revision
