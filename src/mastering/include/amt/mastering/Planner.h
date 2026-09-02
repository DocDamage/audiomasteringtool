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

struct StemMixControls {
  double drums_db{0.0};
  double bass_db{0.0};
  double vocals_db{0.0};
  double other_db{0.0};
};

struct MasteringPlan {
  MasteringCandidatePlan master_a;
  MasteringCandidatePlan master_b;
  StemMixControls stem_mix;
};

enum class MasteringStyle {
  balanced,
  transparent,
  punchy,
  warm,
  wide,
  loud
};

struct MasteringControls {
  MasteringStyle style{MasteringStyle::balanced};
  double target_lufs{-11.0};
  double bass_db{0.0};
  double presence_db{0.0};
  double width{1.0};
  double punch{0.5};
  double warmth{0.2};
  StemMixControls stem_mix;
};

[[nodiscard]] MasteringPlan plan_mastering(const amt::analysis::Phase1AnalysisReport& report);
[[nodiscard]] MasteringPlan plan_mastering(const amt::analysis::AnalysisReport& report);
[[nodiscard]] MasteringControls mastering_style_preset(MasteringStyle style);
void apply_mastering_controls(MasteringPlan& plan,
                              const MasteringControls& controls);

}  // namespace amt::mastering
