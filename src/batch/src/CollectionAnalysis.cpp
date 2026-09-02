#include "amt/batch/CollectionAnalysis.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace amt::batch {

CollectionStats CollectionAnalysis::analyze_collection(
    const std::vector<BatchTrackItem>& tracks) {
  CollectionStats stats;
  stats.track_count = tracks.size();
  if (tracks.empty()) return stats;

  std::vector<double> lufs_values;
  lufs_values.reserve(tracks.size());
  for (const auto& t : tracks) {
    if (t.input_lufs > -70.0) {
      lufs_values.push_back(t.input_lufs);
    }
  }

  if (lufs_values.empty()) return stats;

  std::sort(lufs_values.begin(), lufs_values.end());
  stats.min_lufs = lufs_values.front();
  stats.max_lufs = lufs_values.back();
  stats.dynamic_span_lu = stats.max_lufs - stats.min_lufs;

  double sum = std::accumulate(lufs_values.begin(), lufs_values.end(), 0.0);
  stats.mean_lufs = sum / static_cast<double>(lufs_values.size());
  stats.median_lufs = lufs_values[lufs_values.size() / 2];

  double sq_sum = 0.0;
  for (double val : lufs_values) {
    sq_sum += (val - stats.mean_lufs) * (val - stats.mean_lufs);
  }
  stats.lufs_variance = sq_sum / static_cast<double>(lufs_values.size());

  return stats;
}

CollectionStats CollectionAnalysis::analyze_reports(
    const std::vector<amt::analysis::Phase1AnalysisReport>& reports) {
  std::vector<BatchTrackItem> tracks;
  tracks.reserve(reports.size());
  for (std::size_t i = 0; i < reports.size(); ++i) {
    BatchTrackItem item;
    item.track_index = i;
    item.input_lufs = reports[i].loudness.integrated_lufs;
    item.input_true_peak = reports[i].loudness.true_peak_dbtp;
    tracks.push_back(item);
  }
  return analyze_collection(tracks);
}

}  // namespace amt::batch
