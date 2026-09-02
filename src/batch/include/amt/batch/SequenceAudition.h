#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "amt/batch/BatchProject.h"

namespace amt::batch {

struct TransitionInfo {
  std::size_t from_track_idx{0};
  std::size_t to_track_idx{1};
  double gap_seconds{2.0};
  double loudness_jump_lu{0.0};
  bool has_loudness_jump_warning{false};
};

class SequenceAudition {
 public:
  [[nodiscard]] static std::vector<TransitionInfo> analyze_transitions(
      const BatchAlbumProject& album);
};

}  // namespace amt::batch
