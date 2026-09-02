#pragma once

#include <string>
#include <vector>
#include "amt/revision/RevisionIntent.h"

namespace amt::revision {

struct ConstraintValidationResult {
  bool is_valid{true};
  std::vector<std::string> violated_constraints;
  std::vector<std::string> warnings;
};

class ConstraintResolver {
 public:
  [[nodiscard]] static ConstraintValidationResult validate_constraints(
      const RevisionIntent& intent);
};

}  // namespace amt::revision
