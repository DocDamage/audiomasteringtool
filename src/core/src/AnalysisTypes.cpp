#include "amt/core/AnalysisTypes.h"

namespace amt::core {
bool is_valid_confidence(const float value) noexcept {
  return value >= 0.0F && value <= 1.0F;
}
}
