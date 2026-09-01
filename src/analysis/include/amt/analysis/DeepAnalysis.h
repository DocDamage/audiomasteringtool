#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/analysis/CharacterAnalyzer.h"
#include "amt/analysis/FileAnalyzer.h"
#include "amt/analysis/PerceptualAnalyzer.h"
#include "amt/analysis/StructuralAnalyzer.h"
#include "amt/codec/AudioIO.h"
#include "amt/core/JobControl.h"

namespace amt::analysis {

enum class FindingSeverity { info, low, moderate, high };
enum class FindingCategory { technical, tonal, dynamics, transient, stereo, structure, character };

struct AnalysisFinding {
  std::string id;
  FindingCategory category{FindingCategory::technical};
  FindingSeverity severity{FindingSeverity::info};
  double confidence{0.0};
  bool heuristic{false};
  bool has_time_range{false};
  double start_seconds{0.0};
  double end_seconds{0.0};
  std::string title;
  std::string detail;
  std::vector<std::string> evidence;
};

struct MixHealthDimension {
  std::string id;
  std::string label;
  double heuristic_score{0.0};
  double confidence{0.0};
  std::string assessment;
  std::string rationale;
};

struct MixHealthAssessment {
  std::vector<MixHealthDimension> dimensions;
  double overall_heuristic_score{0.0};
  double overall_confidence{0.0};
  std::string overall_assessment;
};

struct AnalysisReport {
  int schema_version{2};
  Phase1AnalysisReport technical;
  StructuralMetrics structural;
  PerceptualMetrics perceptual;
  CharacterMetrics character;
  MixHealthAssessment mix_health;
  std::vector<AnalysisFinding> findings;
};

[[nodiscard]] std::optional<AnalysisReport> analyze_track(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& path,
    std::string& error,
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

[[nodiscard]] std::string finding_severity_name(FindingSeverity severity);
[[nodiscard]] std::string finding_category_name(FindingCategory category);
[[nodiscard]] std::string analysis_report_to_json(const AnalysisReport& report);

}  // namespace amt::analysis
