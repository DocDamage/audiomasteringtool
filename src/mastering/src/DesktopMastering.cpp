#include "amt/mastering/DesktopMastering.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "amt/mastering/SourceGuidedMastering.h"
#include "amt/separation/ModelArtifactInstaller.h"
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
  bool automatic_mode1_approved{false};
};

[[nodiscard]] bool cancelled(
    const amt::core::CancellationToken* cancellation) noexcept {
  return cancellation != nullptr && cancellation->is_cancelled();
}

void append_unique(std::vector<std::string>& destination,
                   const std::vector<std::string>& source) {
  for (const auto& value : source) {
    if (std::find(destination.begin(), destination.end(), value) ==
        destination.end()) {
      destination.push_back(value);
    }
  }
}

[[nodiscard]] PreparedGuidance stereo_fallback(
    std::string reason,
    std::vector<std::string> warnings = {}) {
  PreparedGuidance prepared;
  prepared.guidance.decision.mode =
      amt::separation::SeparationMode::stereo_mastering;
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
      std::wstring(buffer.data(), static_cast<std::size_t>(length)))
      .parent_path();
#else
  return {};
#endif
}

[[nodiscard]] std::filesystem::path local_app_data_directory() {
#ifdef _WIN32
  const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0U);
  if (required > 1U) {
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required), L'\0');
    const DWORD written = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), required);
    if (written > 0U && written < required) {
      return std::filesystem::path(
          std::wstring(buffer.data(), static_cast<std::size_t>(written)));
    }
  }
  std::error_code temp_error;
  const auto temporary = std::filesystem::temp_directory_path(temp_error);
  return temp_error ? std::filesystem::path{} : temporary;
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

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& component : path) {
    if (component == "..") return false;
  }
  return true;
}

[[nodiscard]] std::filesystem::path per_user_model_artifact(
    const std::filesystem::path& registry_path,
    const std::filesystem::path& packaged_artifact) {
  const auto user_data = local_app_data_directory();
  if (user_data.empty()) return {};

  std::error_code relative_error;
  const auto relative = std::filesystem::relative(
      packaged_artifact, registry_path.parent_path(), relative_error);
  if (relative_error || !safe_relative_path(relative)) return {};
  return user_data / "AudioMasteringTool" / "models" / relative;
}

void add_mode_rationale(MasteringPlan& plan, const std::string& label) {
  const std::string statement = label + ".";
  auto add = [&](MasteringCandidatePlan& candidate) {
    if (std::find(candidate.rationale.begin(), candidate.rationale.end(),
                  statement) == candidate.rationale.end()) {
      candidate.rationale.push_back(statement);
    }
  };
  add(plan.master_a);
  add(plan.master_b);
}

[[nodiscard]] std::string diagnostic_statement(
    const amt::separation::SourceGuidedIssue& issue,
    const bool source_guidance_applied) {
  const int confidence_percent = static_cast<int>(std::lround(
      std::clamp(issue.confidence, 0.0, 1.0) * 100.0));
  const int severity_percent = static_cast<int>(std::lround(
      std::clamp(issue.severity, 0.0, 1.0) * 100.0));

  std::ostringstream output;
  output << (source_guidance_applied ? "Source evidence" : "Source diagnostic")
         << ": " << amt::separation::stem_role_name(issue.source) << ' '
         << amt::separation::source_guided_issue_name(issue.type)
         << " (confidence " << confidence_percent
         << "%, severity " << severity_percent << "%)";
  if (!issue.evidence.empty()) output << " — " << issue.evidence;
  return output.str();
}

void add_source_diagnostics(
    MasteringPlan& plan,
    const std::vector<amt::separation::SourceGuidedIssue>& issues,
    const bool source_guidance_applied) {
  if (issues.empty()) return;

  std::vector<const amt::separation::SourceGuidedIssue*> ranked;
  ranked.reserve(issues.size());
  for (const auto& issue : issues) ranked.push_back(&issue);
  std::stable_sort(
      ranked.begin(), ranked.end(),
      [](const auto* first, const auto* second) {
        const double first_score = first->confidence * first->severity;
        const double second_score = second->confidence * second->severity;
        return first_score > second_score;
      });

  constexpr std::size_t kMaximumVisibleDiagnostics = 6U;
  const std::size_t count =
      std::min(kMaximumVisibleDiagnostics, ranked.size());
  for (std::size_t index = 0U; index < count; ++index) {
    const auto statement =
        diagnostic_statement(*ranked[index], source_guidance_applied);
    if (std::find(plan.master_a.rationale.begin(), plan.master_a.rationale.end(),
                  statement) == plan.master_a.rationale.end()) {
      plan.master_a.rationale.push_back(statement);
    }
  }
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
         registry_error.empty() ? "model registry could not be loaded"
                                : registry_error});
  }
  if (!selection->active_separation_model) {
    auto prepared = stereo_fallback(
        "source guidance unavailable; no production separation model is activated",
        {"Source guidance unavailable — stereo mastering used"});
    append_unique(prepared.guidance.warnings, selection->warnings);
    return prepared;
  }

  auto provider_config = *selection->active_separation_model;
  const auto user_model_artifact = per_user_model_artifact(
      registry_path, provider_config.model_artifact);
  if (user_model_artifact.empty()) {
    auto prepared = stereo_fallback(
        "source guidance unavailable; a writable per-user model location could not be resolved",
        {"Source guidance unavailable — stereo mastering used"});
    append_unique(prepared.guidance.warnings, selection->warnings);
    return prepared;
  }
  provider_config.model_artifact = user_model_artifact;

  std::string install_error;
  const auto install = amt::separation::ensure_model_artifact_installed(
      provider_config, install_error, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, value * 0.28);
      });
  if (!install) {
    if (cancelled(cancellation)) {
      error = install_error.empty() ? "mastering cancelled" : install_error;
      return std::nullopt;
    }
    auto prepared = stereo_fallback(
        "source guidance unavailable; the trusted separation model could not be installed or verified",
        {"Source guidance unavailable — stereo mastering used",
         install_error.empty() ? "trusted model installation was unavailable"
                               : install_error});
    append_unique(prepared.guidance.warnings, selection->warnings);
    return prepared;
  }

  amt::separation::WorkerSeparationProvider provider(provider_config);
  amt::separation::SeparationCache cache(
      output_directory.parent_path() / "separation-cache");
  amt::separation::SourceGuidanceOrchestrator orchestrator(
      provider, nullptr, &cache);

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
        amt::core::report_progress(progress, 0.28 + value * 0.72);
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
  prepared.automatic_mode1_approved = provider_config.automatic_mode1_approved;

  if (!prepared.automatic_mode1_approved &&
      prepared.guidance.decision.mode ==
          amt::separation::SeparationMode::source_guided_stereo) {
    prepared.guidance.decision.mode =
        amt::separation::SeparationMode::stereo_mastering;
    prepared.guidance.decision.reasons.emplace_back(
        "source-estimate diagnostics are available, but automatic source-guided audio changes are not yet approved for this model configuration");
    prepared.guidance.warnings.emplace_back(
        "Source guidance diagnostics available — stereo mastering retained until automatic Mode 1 calibration is approved");
  }

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
  } else if (prepared->analysis_performed &&
             !prepared->automatic_mode1_approved) {
    add_mode_rationale(
        plan,
        "Source diagnostics completed — canonical stereo mastering retained while automatic source-guided changes remain calibration-gated");
  } else if (!prepared->capability_available ||
             result->requested_mode !=
                 amt::separation::SeparationMode::stereo_mastering) {
    add_mode_rationale(plan,
                       "Source guidance unavailable — stereo mastering used");
  } else {
    add_mode_rationale(plan, "Stereo mastering");
  }

  add_source_diagnostics(plan, prepared->issues,
                         result->source_guidance_applied);
  amt::core::report_progress(progress, 1.0);
  return std::move(result->masters);
}

}  // namespace amt::mastering
