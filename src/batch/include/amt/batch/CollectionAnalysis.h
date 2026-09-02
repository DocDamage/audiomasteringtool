#pragma once

#include <vector>
#include "amt/analysis/FileAnalyzer.h"
#include "amt/batch/BatchProject.h"

namespace amt::batch {

struct CollectionStats {
  std::size_t track_count{0};
  double min_lufs{-80.0};
  double max_lufs{-80.0};
  double mean_lufs{-80.0};
  double median_lufs{-80.0};
  double lufs_variance{0.0};
  double dynamic_span_lu{0.0};
};

class CollectionAnalysis {
 public:
  [[nodiscard]] static CollectionStats analyze_collection(
      const std::vector<BatchTrackItem>& tracks);

  [[nodiscard]] static CollectionStats analyze_reports(
      const std::vector<amt::analysis::Phase1AnalysisReport>& reports);
};

}  // namespace amt::batch
