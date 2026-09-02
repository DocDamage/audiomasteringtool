#include "amt/mastering/DesktopMastering.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
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

[[nodiscard]] bool stem_mix_requested(const StemMixControls& controls) {
  return std::abs(controls.drums_db) >= 0.05 ||
         std::abs(controls.bass_db) >= 0.05 ||
         std::abs(controls.vocals_db) >= 0.05 ||
         std::abs(controls.other_db) >= 0.05;
}

[[nodiscard]] double stem_gain_db(const StemMixControls& controls,
                                  const amt::separation::StemRole role) {
  switch (role) {
    case amt::separation::StemRole::drums: return controls.drums_db;
    case amt::separation::StemRole::bass: return controls.bass_db;
    case amt::separation::StemRole::vocals: return controls.vocals_db;
    case amt::separation::StemRole::other: return controls.other_db;
    default: return 0.0;
  }
}

[[nodiscard]] std::optional<std::filesystem::path> render_manual_stem_mix(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::separation::SeparationResult& separation,
    const StemMixControls& controls,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  constexpr std::size_t kBlockFrames = 8192U;
  const std::array<amt::separation::StemRole, 4> roles = {
      amt::separation::StemRole::drums, amt::separation::StemRole::bass,
      amt::separation::StemRole::other, amt::separation::StemRole::vocals};
  std::array<std::filesystem::path, 4> stem_paths;
  for (std::size_t index = 0; index < roles.size(); ++index) {
    const auto artifact = std::find_if(
        separation.artifacts.begin(), separation.artifacts.end(),
        [&](const auto& candidate) {
          return candidate.kind == amt::separation::CacheArtifactKind::stem_audio &&
                 candidate.role == roles[index];
        });
    if (artifact == separation.artifacts.end()) {
      error = "manual stem mixing requires drums, bass, vocals, and other audio";
      return std::nullopt;
    }
    stem_paths[index] = artifact->path;
  }

  std::error_code directory_error;
  const auto mix_directory = output_directory.parent_path() / "manual-stem-mix";
  std::filesystem::create_directories(mix_directory, directory_error);
  if (directory_error) {
    error = "unable to create manual stem-mix directory: " +
            directory_error.message();
    return std::nullopt;
  }
  const auto model_rate_program = mix_directory / "program-model-rate.wav";
  const auto model_rate_mix = mix_directory / "manual-stem-mix-model-rate.wav";
  const auto final_mix = mix_directory / "manual-stem-mix.wav";

  const auto source_metadata = codecs.probe(canonical_input, error);
  if (!source_metadata || source_metadata->channels != 2 ||
      source_metadata->sample_rate <= 0 || separation.sample_rate <= 0) {
    if (error.empty()) error = "manual stem mixing requires valid stereo source audio";
    return std::nullopt;
  }
  amt::codec::ExportRequest prepare_request;
  prepare_request.sample_rate = separation.sample_rate;
  prepare_request.container = amt::codec::AudioContainer::wav;
  prepare_request.sample_format = amt::codec::AudioSampleFormat::float32;
  if (!amt::codec::export_audio(
          codecs, canonical_input, model_rate_program, prepare_request, error,
          cancellation, [&](const double value) {
            amt::core::report_progress(progress, value * 0.15);
          })) {
    return std::nullopt;
  }

  auto program = codecs.open_decoder(model_rate_program, error);
  if (!program) return std::nullopt;
  std::array<std::unique_ptr<amt::codec::IAudioDecoder>, 4> stems;
  for (std::size_t index = 0; index < stems.size(); ++index) {
    stems[index] = codecs.open_decoder(stem_paths[index], error);
    if (!stems[index] ||
        stems[index]->metadata().sample_rate != separation.sample_rate ||
        stems[index]->metadata().channels != 2) {
      if (error.empty()) error = "separation stem geometry is incompatible with manual mixing";
      return std::nullopt;
    }
  }
  auto encoder = codecs.open_encoder(
      model_rate_mix,
      {.sample_rate = separation.sample_rate,
       .channels = 2,
       .container = amt::codec::AudioContainer::wav,
       .sample_format = amt::codec::AudioSampleFormat::float32,
       .tags = {}},
      error);
  if (!encoder) return std::nullopt;

  std::int64_t rendered_frames = 0;
  while (true) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "manual stem mixing cancelled";
      return std::nullopt;
    }
    amt::audio::AudioBuffer mixed;
    std::size_t program_frames = 0U;
    if (!program->read(mixed, kBlockFrames, program_frames, error, cancellation)) {
      return std::nullopt;
    }
    std::array<amt::audio::AudioBuffer, 4> blocks;
    for (std::size_t index = 0; index < stems.size(); ++index) {
      std::size_t stem_frames = 0U;
      if (!stems[index]->read(blocks[index], kBlockFrames, stem_frames, error,
                              cancellation)) {
        return std::nullopt;
      }
      if (stem_frames != program_frames) {
        error = "manual stem mix encountered mismatched source/stem frame counts";
        return std::nullopt;
      }
    }
    if (program_frames == 0U) break;

    for (std::size_t index = 0; index < stems.size(); ++index) {
      const double delta = std::pow(10.0, stem_gain_db(controls, roles[index]) / 20.0) - 1.0;
      if (std::abs(delta) < 1.0e-9) continue;
      for (std::size_t channel = 0; channel < 2U; ++channel) {
        for (std::size_t frame = 0; frame < program_frames; ++frame) {
          const double value = static_cast<double>(mixed.channel(channel)[frame]) +
                               static_cast<double>(blocks[index].channel(channel)[frame]) * delta;
          mixed.channel(channel)[frame] = static_cast<float>(
              std::clamp(std::isfinite(value) ? value : 0.0, -8.0, 8.0));
        }
      }
    }
    if (!encoder->write(mixed, error, cancellation)) return std::nullopt;
    rendered_frames += static_cast<std::int64_t>(program_frames);
    if (program->metadata().frames > 0) {
      amt::core::report_progress(
          progress, 0.15 + 0.70 * static_cast<double>(rendered_frames) /
                               static_cast<double>(program->metadata().frames));
    }
  }
  if (!encoder->finalize(error)) return std::nullopt;

  amt::codec::ExportRequest restore_request;
  restore_request.sample_rate = source_metadata->sample_rate;
  restore_request.container = amt::codec::AudioContainer::wav;
  restore_request.sample_format = amt::codec::AudioSampleFormat::float32;
  if (!amt::codec::export_audio(
          codecs, model_rate_mix, final_mix, restore_request, error,
          cancellation, [&](const double value) {
            amt::core::report_progress(progress, 0.85 + value * 0.15);
          })) {
    return std::nullopt;
  }
  amt::core::report_progress(progress, 1.0);
  return final_mix;
}

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

[[nodiscard]] std::string json_escape(const std::string& text) {
  std::ostringstream output;
  for (const unsigned char c : text) {
    switch (c) {
      case '\\': output << "\\\\"; break;
      case '"': output << "\\\""; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (c < 0x20U) output << '?';
        else output << static_cast<char>(c);
        break;
    }
  }
  return output.str();
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

void fill_desktop_report(
    DesktopMasteringReport& report,
    const PreparedGuidance& prepared,
    const SourceGuidedMasteringRenderPair& rendered,
    const bool manual_stem_mix_applied) {
  report.source_diagnostics_performed = prepared.analysis_performed;
  report.source_guidance_applied = rendered.source_guidance_applied;
  report.manual_stem_mix_applied = manual_stem_mix_applied;
  report.automatic_mode1_approved = prepared.automatic_mode1_approved;

  std::ostringstream summary;
  if (manual_stem_mix_applied) {
    summary << "Manual drums/bass/vocals/other stem balance was applied before mastering.";
  } else if (!prepared.analysis_performed) {
    summary << "Source diagnostics were unavailable; canonical stereo mastering was used.";
  } else if (rendered.source_guidance_applied) {
    summary << "Source diagnostics completed and source-guided stereo processing was applied.";
  } else if (!prepared.automatic_mode1_approved) {
    summary << "Source diagnostics completed; canonical stereo mastering was retained because automatic Mode 1 remains calibration-gated.";
  } else {
    summary << "Source diagnostics completed; measured evidence did not require source-guided processing.";
  }
  if (!prepared.issues.empty()) {
    summary << " " << prepared.issues.size() << " source-specific issue(s) were measured.";
  }
  report.summary = summary.str();

  std::ostringstream json;
  json << "{\n"
       << "  \"schemaVersion\": 1,\n"
       << "  \"sourceDiagnosticsPerformed\": "
       << (report.source_diagnostics_performed ? "true" : "false") << ",\n"
       << "  \"sourceGuidanceApplied\": "
       << (report.source_guidance_applied ? "true" : "false") << ",\n"
       << "  \"manualStemMixApplied\": "
       << (report.manual_stem_mix_applied ? "true" : "false") << ",\n"
       << "  \"automaticMode1Approved\": "
       << (report.automatic_mode1_approved ? "true" : "false") << ",\n"
       << "  \"requestedMode\": \""
       << amt::separation::separation_mode_name(rendered.requested_mode) << "\",\n"
       << "  \"renderedMode\": \""
       << amt::separation::separation_mode_name(rendered.rendered_mode) << "\",\n"
       << "  \"appliedBindings\": " << rendered.applied_bindings << ",\n"
       << "  \"summary\": \"" << json_escape(report.summary) << "\",\n"
       << "  \"issues\": [\n";
  for (std::size_t index = 0U; index < prepared.issues.size(); ++index) {
    const auto& issue = prepared.issues[index];
    json << "    {\"source\": \""
         << amt::separation::stem_role_name(issue.source)
         << "\", \"type\": \""
         << amt::separation::source_guided_issue_name(issue.type)
         << "\", \"severity\": " << issue.severity
         << ", \"confidence\": " << issue.confidence
         << ", \"evidence\": \"" << json_escape(issue.evidence) << "\"}";
    if (index + 1U < prepared.issues.size()) json << ',';
    json << '\n';
  }
  json << "  ],\n  \"warnings\": [\n";
  for (std::size_t index = 0U; index < rendered.warnings.size(); ++index) {
    json << "    \"" << json_escape(rendered.warnings[index]) << "\"";
    if (index + 1U < rendered.warnings.size()) json << ',';
    json << '\n';
  }
  json << "  ]\n}\n";
  report.json = json.str();
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
  // This build is used privately and does not redistribute the downloaded
  // weights. Keep truthful licence metadata without treating commercial
  // redistribution eligibility as a prerequisite for local inference.
  workflow_config.guidance.require_bundled_production_model_eligibility = false;
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
    const amt::core::ProgressCallback& progress,
    DesktopMasteringReport* report) {
  error.clear();
  const bool wants_manual_stem_mix = stem_mix_requested(plan.stem_mix);
  auto prepared = prepare_guidance(
      codecs, canonical_input, output_directory, source_analysis, error,
      cancellation,
      [&](const double value) {
        amt::core::report_progress(
            progress, value * (wants_manual_stem_mix ? 0.30 : 0.35));
      });
  if (!prepared) return std::nullopt;

  std::filesystem::path render_input = canonical_input;
  if (wants_manual_stem_mix) {
    if (!prepared->guidance.separation) {
      error = "manual stem mixing was requested, but source separation is unavailable";
      return std::nullopt;
    }
    auto mixed = render_manual_stem_mix(
        codecs, canonical_input, output_directory,
        *prepared->guidance.separation, plan.stem_mix, error, cancellation,
        [&](const double value) {
          amt::core::report_progress(progress, 0.30 + value * 0.25);
        });
    if (!mixed) return std::nullopt;
    render_input = std::move(*mixed);
    prepared->guidance.decision.mode =
        amt::separation::SeparationMode::stereo_mastering;
  }

  auto result = render_mastering_plan_with_source_guidance(
      codecs, render_input, output_directory, source_analysis, plan,
      prepared->guidance, prepared->issues, error, {}, settings, cancellation,
      [&](const double value) {
        const double start = wants_manual_stem_mix ? 0.55 : 0.35;
        amt::core::report_progress(progress, start + value * (1.0 - start));
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
  if (report != nullptr) {
    fill_desktop_report(*report, *prepared, *result,
                        wants_manual_stem_mix);
  }
  amt::core::report_progress(progress, 1.0);
  return std::move(result->masters);
}

}  // namespace amt::mastering
