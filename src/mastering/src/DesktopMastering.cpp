#include "amt/mastering/DesktopMastering.h"

#include <utility>

#include "amt/mastering/SourceGuidedMastering.h"
#include "amt/separation/SourceGuidance.h"

namespace amt::mastering {

std::optional<MasteringRenderPair> render_mastering_plan_for_desktop(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    MasteringPlan& plan,
    std::string& error,
    const RenderSettings& settings,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  // No production separation model/provider is registered yet. The desktop still
  // goes through the Phase 5 wrapper now, but explicitly requests the guaranteed
  // stereo path instead of fabricating source guidance. When a production provider
  // is wired, this is the single desktop integration point that supplies its real
  // SourceGuidanceResult and evidence-backed SourceGuidedIssue objects.
  amt::separation::SourceGuidanceResult guidance;
  guidance.decision.mode = amt::separation::SeparationMode::stereo_mastering;
  guidance.decision.confidence = 1.0;
  guidance.decision.artifact_risk = 0.0;
  guidance.decision.reasons.emplace_back(
      "No production separation provider is registered; stereo mastering is used.");
  guidance.warnings.emplace_back(
      "Source guidance unavailable — stereo mastering used");

  auto result = render_mastering_plan_with_source_guidance(
      codecs, canonical_input, output_directory, source_analysis, plan, guidance, {},
      error, {}, settings, cancellation, progress);
  if (!result) return std::nullopt;

  // Critical provenance rule: the desktop state and project-history code must see
  // the exact plan that produced the rendered masters. In Mode 0/fallback this is
  // the caller plan; in real Mode 1 it is the post-guidance re-analysis/replan.
  plan = std::move(result->effective_plan);
  return std::move(result->masters);
}

}  // namespace amt::mastering
