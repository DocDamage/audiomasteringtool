#include "amt/batch/CohesionPlanner.h"
#include <algorithm>
#include <cmath>

namespace amt::batch {

std::vector<TrackCohesionPlan> CohesionPlanner::plan_cohesion(
    const std::vector<BatchTrackItem>& tracks,
    double target_album_lufs,
    double target_ceiling_dbtp) {
  std::vector<TrackCohesionPlan> plans;
  plans.reserve(tracks.size());

  auto stats = CollectionAnalysis::analyze_collection(tracks);
  double ref_lufs = (stats.track_count > 0 && stats.median_lufs > -70.0)
                        ? stats.median_lufs
                        : target_album_lufs;

  for (const auto& track : tracks) {
    TrackCohesionPlan plan;
    plan.track_index = track.track_index;
    plan.target_ceiling_dbtp = target_ceiling_dbtp;

    if (track.input_lufs <= -70.0) {
      plan.target_lufs = target_album_lufs;
      plan.relative_offset_lu = 0.0;
      plan.explanation = "Default album target applied";
    } else {
      // Calculate delta from album median
      double delta_from_median = track.input_lufs - ref_lufs;

      // Preserve 50% of the relative artistic contrast between tracks
      double adjusted_offset = delta_from_median * 0.50;

      // Clamp offset to +/- 2.5 LU so extremes don't jump too wildly
      adjusted_offset = std::clamp(adjusted_offset, -2.5, 2.5);

      plan.target_lufs = target_album_lufs + adjusted_offset;
      plan.relative_offset_lu = adjusted_offset;

      if (std::abs(adjusted_offset) < 0.2) {
        plan.explanation = "Aligned to core album loudness baseline";
      } else if (adjusted_offset > 0.0) {
        plan.explanation = "High-energy track: preserved relative loudness punch (+ " +
                           std::to_string(adjusted_offset).substr(0, 3) + " LU)";
      } else {
        plan.explanation = "Intimate/dynamic track: preserved intentional softer nuance (" +
                           std::to_string(adjusted_offset).substr(0, 4) + " LU)";
      }
    }

    plans.push_back(plan);
  }

  return plans;
}

}  // namespace amt::batch
