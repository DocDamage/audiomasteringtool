#pragma once

#include <vector>
#include "amt/batch/BatchProject.h"
#include "amt/batch/CollectionAnalysis.h"

namespace amt::batch {

struct TrackCohesionPlan {
  std::size_t track_index{0};
  double target_lufs{-14.0};
  double target_ceiling_dbtp{-1.0};
  double relative_offset_lu{0.0};
  std::string explanation;
};

class CohesionPlanner {
 public:
  [[nodiscard]] static std::vector<TrackCohesionPlan> plan_cohesion(
      const std::vector<BatchTrackItem>& tracks,
      double target_album_lufs = -14.0,
      double target_ceiling_dbtp = -1.0);
};

}  // namespace amt::batch
