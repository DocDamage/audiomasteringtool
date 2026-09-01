#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/core/AnalysisTypes.h"
#include "amt/codec/AudioIO.h"
#include "amt/mastering/Planner.h"
#include "amt/project/ProjectStore.h"

namespace amt::app {

enum class AppState {
  idle,
  loading_file,
  analyzing,
  mastering,
  ready,
  exporting,
  error
};

enum class AuditionTarget {
  original,
  master_a,
  master_b
};

struct DiagnosticSummary {
  bool diagnostics_performed{false};
  bool guidance_applied{false};
  bool automatic_mode1_approved{false};
  std::string summary;
  std::string raw_json;
  std::vector<std::string> detected_instruments;
  std::vector<std::string> detected_interactions;
  std::vector<std::string> recommended_repairs;
};

struct TrackViewModel {
  std::filesystem::path source_path;
  std::string file_name;
  std::int64_t frames{0};
  int sample_rate{0};
  int channels{0};
  std::string format_description;

  double integrated_lufs{-70.0};
  double true_peak_dbtp{-70.0};
  double loudness_range_lu{0.0};

  bool has_analysis{false};
  std::string musical_key;
  double estimated_bpm{0.0};

  bool has_masters{false};
  std::filesystem::path master_a_path;
  double master_a_lufs{-70.0};
  double master_a_true_peak{-70.0};
  std::vector<std::string> master_a_rationale;

  std::filesystem::path master_b_path;
  double master_b_lufs{-70.0};
  double master_b_true_peak{-70.0};
  std::vector<std::string> master_b_rationale;

  AuditionTarget selected_candidate{AuditionTarget::master_a};
  DiagnosticSummary diagnostics;
};

}  // namespace amt::app
