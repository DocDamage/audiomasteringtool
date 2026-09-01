#pragma once

#include <string>
#include <vector>

#include "amt/analysis/DeepAnalysis.h"
#include "amt/analysis/FileAnalyzer.h"
#include "amt/mastering/ProcessingGraph.h"

namespace amt::mastering {

struct MasteringCandidatePlan {
  std::string id;
  std::string name;
  bool recommended{false};
  double target_lufs{-10.0};
  double ceiling_dbtp{-1.0};
  double preservation_bias{0.5};
  std::vector<std::string> rationale;
  ProcessingGraph graph;
};

struct MasteringPlan {
  MasteringCandidatePlan master_a;
  MasteringCandidatePlan master_b;
};

[[nodiscard]] MasteringPlan plan_mastering(const amt::analysis::Phase1AnalysisReport& report);
[[nodiscard]] MasteringPlan plan_mastering(const amt::analysis::AnalysisReport& report);

}  // namespace amt::mastering
