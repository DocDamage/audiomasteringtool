#include "amt/separation/SourceGuidedWorkflow.h"

#include <algorithm>
#include <string>
#include <utility>

namespace amt::separation {
namespace {

[[nodiscard]] bool cancelled(const amt::core::CancellationToken* cancellation) noexcept {
  return cancellation != nullptr && cancellation->is_cancelled();
}

void append_warnings(std::vector<std::string>& destination,
                     const std::vector<std::string>& source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

[[nodiscard]] ArtifactAssessment unknown_artifact_assessment() {
  return {.overall_risk = 1.0,
          .confidence = 0.0,
          .evidence = {"reconstruction is not part of automatic source-guided diagnosis"}};
}

}  // namespace

std::optional<SourceGuidedWorkflowResult> evaluate_source_guided_workflow(
    SourceGuidanceOrchestrator& orchestrator,
    amt::codec::ICodecService& codecs,
    const amt::analysis::Phase1AnalysisReport& canonical_analysis,
    SourceGuidanceRequest request,
    std::string& error,
    const SourceGuidedWorkflowConfig& config,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  SourceGuidedWorkflowResult output;

  if (request.separation.source_path.empty()) {
    error = "source-guided workflow is missing the canonical source path";
    return std::nullopt;
  }
  if (cancelled(cancellation)) {
    error = "source-guided workflow cancelled";
    return std::nullopt;
  }

  // First pass: separation is permitted strictly as diagnostic evidence gathering.
  // Generic stereo analysis is not converted into source attribution here.
  auto diagnostic_config = config.guidance;
  diagnostic_config.allow_diagnostic_separation = true;
  request.evidence.source_specific_issue = false;
  request.evidence.reconstruction_required_for_full_repair = false;
  request.evidence.expected_repair_benefit = 0.0;
  request.evidence.source_guidance_confidence = 0.0;
  request.evidence.source_guided_stereo_sufficiency = 0.0;

  auto guidance = orchestrator.execute(
      request, error, diagnostic_config, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, value * 0.55);
      });
  if (!guidance) return std::nullopt;

  append_warnings(output.warnings, guidance->warnings);
  output.guidance = std::move(*guidance);
  if (!output.guidance.separation) {
    // Provider/model unavailability is a normal product fallback, not a workflow
    // failure. The orchestrator has already returned a truthful stereo decision.
    amt::core::report_progress(progress, 1.0);
    return output;
  }

  if (cancelled(cancellation)) {
    error = "source-guided workflow cancelled";
    return std::nullopt;
  }

  auto inferred = infer_source_guided_issues(
      codecs, canonical_analysis, *output.guidance.separation, error,
      config.issue_inference, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, 0.55 + value * 0.45);
      });
  if (!inferred) return std::nullopt;

  output.source_estimates_analyzed = true;
  output.issues = std::move(inferred->issues);
  output.evidence = inferred->evidence;
  output.measurement_confidence = inferred->measurement_confidence;
  append_warnings(output.warnings, inferred->warnings);

  // Automatic Phase 5 diagnosis only authorizes Mode 0/1. A future explicit Mode 2
  // request must still go through the existing complete-reconstruction/artifact
  // evaluator path rather than inheriting a diagnostic-separation decision.
  output.evidence.reconstruction_required_for_full_repair = false;
  output.evidence.model_confidence = std::min(
      output.evidence.model_confidence,
      output.guidance.separation->overall_confidence);

  output.guidance.decision = choose_separation_mode(
      output.evidence, unknown_artifact_assessment(), config.guidance.policy);
  if (output.guidance.decision.mode == SeparationMode::stem_reconstruction) {
    output.guidance.decision.mode = SeparationMode::source_guided_stereo;
    output.guidance.decision.reasons.emplace_back(
        "automatic diagnostic workflow does not authorize stem reconstruction");
  }

  if (output.guidance.decision.mode == SeparationMode::source_guided_stereo &&
      output.issues.empty()) {
    output.guidance.decision.mode = SeparationMode::stereo_mastering;
    output.guidance.decision.reasons.emplace_back(
        "no evidence-backed source issues survived the intervention confidence gate");
  }

  amt::core::report_progress(progress, 1.0);
  return output;
}

}  // namespace amt::separation
