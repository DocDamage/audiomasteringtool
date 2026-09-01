#include "amt/mastering/DesktopMastering.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "amt/mastering/SourceGuidedMastering.h"
#include "amt/separation/ModelRegistry.h"
#include "amt/separation/SourceGuidedWorkflow.h"
#include "amt/separation/WorkerSeparationProvider.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace amt::mastering {
namespace {

struct PreparedGuidance {
  amt::separation::SourceGuidanceResult guidance;
  std::vector<amt::separation::SourceGuidedIssue> issues;
  bool capability_available{false};
  bool analysis_performed{false};
};

[[nodiscard]] bool cancelled(const amt::core::CancellationToken* cancellation) noexcept {
  return cancellation != nullptr && cancellation->is_cancelled();
}

void append_unique(std::vector<std::string>& destination,
                   const std::vector<std::string>& source) {
  for (const auto& value : source) {
    if (std::find(destination.begin(), destination.end(), value) == destination.end()) {
      destination.push_back(value);
    }
  }
}

[[nodiscard]] PreparedGuidance stereo_fallback(std::string reason,
                                               std::vector<std::string> warnings = {}) {
  PreparedGuidance prepared;
  prepared.guidance.decision.mode = amt::separation::SeparationMode::stereo_mastering;
  prepared.guidance.decision.confidence = 1.0;
  prepared.guidance.decision.artifact_risk = 0.0;
  prepared.guidance.decision.reasons.push_back(std::move(reason));
  prepared.guidance.warnings = std::move(warnings);
  return prepared;
}

[[nodiscard]] std::filesystem::path application_directory() {
#ifdef _WIN32
  std::vector<wchar_t> buffer(32768U, L'\0');
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0U || length >= buffer.size()) return {};
  return std::filesystem::path(
      std::wstring(buffer.data(), static_cast<std::size_t>(length))).parent_path();
#else
  return {};
#endif
}

[[nodiscard]] std::filesystem::path packaged_worker_path(
    const std::filesystem::path& app_directory) {
#ifdef _WIN32
  return app_directory / "amt_worker.exe";
#else
  return app_directory / "amt_worker";
#endif
}

void add_mode_rationale(MasteringPlan& plan, const std::string& label) {
  const std::string statement = label + ".";
  auto add = [&](MasteringCandidatePlan& candidate) {
    if (std::find(candidate.rationale.begin(), candidate.rationale.end(), statement) ==
        candidate.rationale.end()) {
      candidate.rationale.push_back(statement);
    }
  };
  add(plan.master_a);
  add(plan.master_b);
}

[[nodiscard]] std::optional<PreparedGuidance> prepare_guidance(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (cancelled(cancellation)) {
    error = "mastering cancelled";
    return std::nullopt;
  }

  const auto app_directory = application_directory();
  if (app_directory.empty()) {
    return stereo_fallback(
        "source guidance unavailable; the packaged application directory could not be resolved",
        {"Source guidance unavailable — stereo mastering used"});
  }

  const auto registry_path = app_directory / "models" / "registry.json";
  std::error_code registry_exists_error;
  if (!std::filesystem::is_regular_file(registry_path, registry_exists_error) ||
      registry_exists_error) {
    return stereo_fallback(
        "source guidance unavailable; no packaged model registry is present",
        {"Source guidance unavailable — stereo mastering used"});
  }

  std::string registry_error;
  auto selection = amt::separation::load_model_registry_selection(
      registry_path, packaged_worker_path(app_directory), registry_error,
      output_directory.parent_path() / "source-estimates");
  if (!selection) {
    return stereo_fallback(
        "source guidance unavailable; the packaged model registry is invalid",
        {"Source guidance unavailable — stereo mastering used",
         registry_error.empty() ? "model registry could not be loaded" : registry_error});
  }
  if (!selection->active_separation_model) {
    auto prepared = stereo_fallback(
        "source guidance unavailable; no production separation model is activated",
        {"Source guidance unavailable — stereo mastering used"});
    append_unique(prepared.guidance.warnings, selection->warnings);
    return prepared;
  }

  amt::separation::WorkerSeparationProvider provider(
      *selection->active_separation_model);
  amt::separation::SeparationCache cache(
      output_directory.parent_path() / "separation-cache");
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider, nullptr, &cache);

  amt::separation::SourceGuidanceRequest request;
  request.separation.source_path = canonical_input;
  request.separation.requested_stems = provider.model_manifest().stem_taxonomy;
  request.separation.request_stem_audio = true;
  request.separation.request_time_frequency_masks = false;

  amt::separation::SourceGuidedWorkflowConfig workflow_config;
  workflow_config.guidance.require_bundled_production_model_eligibility = true;
  workflow_config.guidance.enable_cache = true;
  workflow_config.guidance.compute_missing_source_fingerprint = true;

  std::string workflow_error;
  auto workflow = amt::separation::evaluate_source_guided_workflow(
      orchestrator, codecs, source_analysis, std::move(request), workflow_error,
      workflow_config, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, value);
      });
  if (!workflow) {
    if (cancelled(cancellation)) {
      error = workflow_error.empty() ? "mastering cancelled" : workflow_error;
      return std::nullopt;
    }
    auto prepared = stereo_fallback(
        "source guidance unavailable; source-estimate diagnostics failed safely",
        {"Source guidance unavailable — stereo mastering used",
         workflow_error.empty() ? "source-estimate diagnostics were unavailable"
                                : workflow_error});
    append_unique(prepared.guidance.warnings, selection->warnings);
    return prepared;
  }

  PreparedGuidance prepared;
  prepared.guidance = std::move(workflow->guidance);
  prepared.issues = std::move(workflow->issues);
  prepared.analysis_performed = workflow->source_estimates_analyzed;
  prepared.capability_available = workflow->source_estimates_analyzed &&
                                  prepared.guidance.separation.has_value();
  append_unique(prepared.guidance.warnings, selection->warnings);
  append_unique(prepared.guidance.warnings, workflow->warnings);
  return prepared;
}

}  // namespace

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
  error.clear();
  auto prepared = prepare_guidance(
      codecs, canonical_input, output_directory, source_analysis, error,
      cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, value * 0.35);
      });
  if (!prepared) return std::nullopt;

  auto result = render_mastering_plan_with_source_guidance(
      codecs, canonical_input, output_directory, source_analysis, plan,
      prepared->guidance, prepared->issues, error, {}, settings, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, 0.35 + value * 0.65);
      });
  if (!result) return std::nullopt;

  plan = std::move(result->effective_plan);
  if (result->source_guidance_applied) {
    add_mode_rationale(plan, "Source guidance used");
  } else if (!prepared->capability_available ||
             result->requested_mode != amt::separation::SeparationMode::stereo_mastering) {
    add_mode_rationale(plan, "Source guidance unavailable — stereo mastering used");
  } else {
    add_mode_rationale(plan, "Stereo mastering");
  }

  amt::core::report_progress(progress, 1.0);
  return std::move(result->masters);
}

}  // namespace amt::mastering
