#pragma once

#include <optional>
#include <string>
#include <vector>

#include "amt/analysis/FileAnalyzer.h"
#include "amt/codec/AudioIO.h"
#include "amt/separation/SourceGuidance.h"
#include "amt/separation/SourceIssueInference.h"

namespace amt::separation {

struct SourceGuidedWorkflowConfig {
  SourceGuidanceConfig guidance;
  SourceIssueInferenceConfig issue_inference;
};

struct SourceGuidedWorkflowResult {
  SourceGuidanceResult guidance;
  SourceInterventionEvidence evidence;
  std::vector<SourceGuidedIssue> issues;
  double measurement_confidence{0.0};
  bool source_estimates_analyzed{false};
  std::vector<std::string> warnings;
};

// Evidence-first Mode 1 evaluation:
//   canonical analysis -> diagnostic separation -> stem measurements ->
//   source-specific evidence -> policy decision.
// Diagnostic separation alone never authorizes source-guided processing.
[[nodiscard]] std::optional<SourceGuidedWorkflowResult> evaluate_source_guided_workflow(
    SourceGuidanceOrchestrator& orchestrator,
    amt::codec::ICodecService& codecs,
    const amt::analysis::Phase1AnalysisReport& canonical_analysis,
    SourceGuidanceRequest request,
    std::string& error,
    const SourceGuidedWorkflowConfig& config = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::separation
