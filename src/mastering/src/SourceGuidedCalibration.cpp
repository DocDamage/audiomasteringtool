#include "amt/mastering/SourceGuidedCalibration.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "amt/analysis/FileAnalyzer.h"
#include "amt/audio/AudioBuffer.h"
#include "amt/mastering/Planner.h"
#include "amt/mastering/SourceGuidedMastering.h"
#include "amt/separation/ModelArtifactInstaller.h"
#include "amt/separation/ModelRegistry.h"
#include "amt/separation/SourceGuidedWorkflow.h"
#include "amt/separation/WorkerSeparationProvider.h"

namespace amt::mastering {
namespace {

constexpr std::size_t kAuditionReadFrames = 8192U;

[[nodiscard]] bool cancelled(
    const amt::core::CancellationToken* cancellation) noexcept {
  return cancellation != nullptr && cancellation->is_cancelled();
}

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& component : path) {
    if (component == "..") return false;
  }
  return true;
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
  const auto value = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
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

[[nodiscard]] std::filesystem::path resolve_model_artifact(
    const SourceGuidedCalibrationRequest& request,
    const std::filesystem::path& registry_artifact,
    std::string& error) {
  std::error_code relative_error;
  const auto relative = std::filesystem::relative(
      registry_artifact, request.registry_path.parent_path(), relative_error);
  if (relative_error || !safe_relative_path(relative)) {
    error = "calibration model artifact could not be mapped into the model store";
    return {};
  }
  return request.model_store_root / relative;
}

bool render_attenuated_audition_copy(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    const double gain_db,
    std::string& error,
    const amt::core::CancellationToken* cancellation) {
  if (!std::isfinite(gain_db) || gain_db > 1.0e-9) {
    error = "calibration audition gain must be finite attenuation only";
    return false;
  }

  auto decoder = codecs.open_decoder(input, error);
  if (!decoder) return false;
  const auto metadata = decoder->metadata();
  if (metadata.sample_rate <= 0 || metadata.channels <= 0) {
    error = "calibration audition source has invalid audio metadata";
    return false;
  }

  std::error_code directory_error;
  std::filesystem::create_directories(output.parent_path(), directory_error);
  if (directory_error) {
    error = "unable to create calibration audition directory: " +
            directory_error.message();
    return false;
  }

  amt::codec::EncodeSettings settings;
  settings.sample_rate = metadata.sample_rate;
  settings.channels = metadata.channels;
  settings.container = amt::codec::AudioContainer::wav;
  settings.sample_format = amt::codec::AudioSampleFormat::float32;
  auto encoder = codecs.open_encoder(output, settings, error);
  if (!encoder) return false;

  const float gain = static_cast<float>(std::pow(10.0, gain_db / 20.0));
  while (true) {
    if (cancelled(cancellation)) {
      error = "source-guided calibration cancelled";
      return false;
    }
    amt::audio::AudioBuffer block;
    std::size_t frames_read = 0U;
    if (!decoder->read(block, kAuditionReadFrames, frames_read, error, cancellation)) {
      return false;
    }
    if (frames_read == 0U) break;
    for (std::size_t channel = 0U; channel < block.channels(); ++channel) {
      auto samples = block.channel(channel);
      for (auto& sample : samples) sample *= gain;
    }
    if (!encoder->write(block, error, cancellation)) return false;
  }
  return encoder->finalize(error);
}

bool make_level_matched_auditions(
    amt::codec::ICodecService& codecs,
    const MasteringRenderPair& stereo,
    const SourceGuidedMasteringRenderPair& guided,
    SourceGuidedCalibrationResult& result,
    const std::filesystem::path& output_directory,
    std::string& error,
    const amt::core::CancellationToken* cancellation) {
  const double stereo_lufs = stereo.master_a.analysis.loudness.integrated_lufs;
  const double guided_lufs = guided.masters.master_a.analysis.loudness.integrated_lufs;
  if (!std::isfinite(stereo_lufs) || !std::isfinite(guided_lufs)) {
    error = "calibration candidates have invalid loudness measurements";
    return false;
  }

  result.audition_reference_lufs = std::min(stereo_lufs, guided_lufs);
  result.stereo_audition_gain_db = result.audition_reference_lufs - stereo_lufs;
  result.guided_audition_gain_db = result.audition_reference_lufs - guided_lufs;
  result.stereo_blind_audition = output_directory / "blind" / "stereo.wav";
  result.guided_blind_audition = output_directory / "blind" / "guided.wav";

  if (!render_attenuated_audition_copy(
          codecs, stereo.master_a.output_path, result.stereo_blind_audition,
          result.stereo_audition_gain_db, error, cancellation)) {
    return false;
  }
  if (!render_attenuated_audition_copy(
          codecs, guided.masters.master_a.output_path, result.guided_blind_audition,
          result.guided_audition_gain_db, error, cancellation)) {
    return false;
  }
  return true;
}

bool write_manifest(const SourceGuidedCalibrationRequest& request,
                    const SourceGuidedCalibrationResult& result,
                    const SourceGuidedMasteringRenderPair* guided,
                    const MasteringRenderPair& stereo,
                    std::string& error) {
  std::ostringstream output;
  output << "{\n"
         << "  \"schemaVersion\": 1,\n"
         << "  \"source\": \"" << json_escape(path_utf8(request.source_path)) << "\",\n"
         << "  \"modelName\": \"" << json_escape(result.model_name) << "\",\n"
         << "  \"modelVersion\": \"" << json_escape(result.model_version) << "\",\n"
         << "  \"evidenceMode\": \""
         << amt::separation::separation_mode_name(result.evidence_mode) << "\",\n"
         << "  \"sourceEstimatesAnalyzed\": "
         << (result.source_estimates_analyzed ? "true" : "false") << ",\n"
         << "  \"separationProviderInvoked\": "
         << (result.separation_provider_invoked ? "true" : "false") << ",\n"
         << "  \"separationCacheHit\": "
         << (result.separation_cache_hit ? "true" : "false") << ",\n"
         << "  \"guidedCandidateRendered\": "
         << (result.guided_candidate_rendered ? "true" : "false") << ",\n"
         << "  \"stereoMasterA\": \""
         << json_escape(path_utf8(result.stereo_master_a)) << "\",\n"
         << "  \"stereoMasterALufs\": "
         << stereo.master_a.analysis.loudness.integrated_lufs << ",\n"
         << "  \"stereoMasterATruePeakDbtp\": "
         << stereo.master_a.analysis.loudness.true_peak_dbtp << ",\n"
         << "  \"guidedMasterA\": \""
         << json_escape(path_utf8(result.guided_master_a)) << "\",\n";

  if (guided != nullptr) {
    output << "  \"guidedMasterALufs\": "
           << guided->masters.master_a.analysis.loudness.integrated_lufs << ",\n"
           << "  \"guidedMasterATruePeakDbtp\": "
           << guided->masters.master_a.analysis.loudness.true_peak_dbtp << ",\n"
           << "  \"guidedAppliedBindings\": " << guided->applied_bindings << ",\n"
           << "  \"auditionReferenceLufs\": " << result.audition_reference_lufs << ",\n"
           << "  \"stereoAuditionGainDb\": " << result.stereo_audition_gain_db << ",\n"
           << "  \"guidedAuditionGainDb\": " << result.guided_audition_gain_db << ",\n"
           << "  \"stereoBlindAudition\": \""
           << json_escape(path_utf8(result.stereo_blind_audition)) << "\",\n"
           << "  \"guidedBlindAudition\": \""
           << json_escape(path_utf8(result.guided_blind_audition)) << "\",\n";
  } else {
    output << "  \"guidedMasterALufs\": null,\n"
           << "  \"guidedMasterATruePeakDbtp\": null,\n"
           << "  \"guidedAppliedBindings\": 0,\n"
           << "  \"auditionReferenceLufs\": null,\n"
           << "  \"stereoAuditionGainDb\": null,\n"
           << "  \"guidedAuditionGainDb\": null,\n"
           << "  \"stereoBlindAudition\": \"\",\n"
           << "  \"guidedBlindAudition\": \"\",\n";
  }

  output << "  \"issues\": [\n";
  for (std::size_t index = 0U; index < result.issues.size(); ++index) {
    const auto& issue = result.issues[index];
    output << "    {\"source\": \""
           << amt::separation::stem_role_name(issue.source)
           << "\", \"type\": \""
           << amt::separation::source_guided_issue_name(issue.type)
           << "\", \"severity\": " << issue.severity
           << ", \"confidence\": " << issue.confidence
           << ", \"evidence\": \"" << json_escape(issue.evidence) << "\"}";
    if (index + 1U < result.issues.size()) output << ',';
    output << '\n';
  }
  output << "  ],\n  \"warnings\": [\n";
  for (std::size_t index = 0U; index < result.warnings.size(); ++index) {
    output << "    \"" << json_escape(result.warnings[index]) << "\"";
    if (index + 1U < result.warnings.size()) output << ',';
    output << '\n';
  }
  output << "  ]\n}\n";

  std::ofstream file(result.manifest_path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "unable to create calibration manifest";
    return false;
  }
  const auto text = output.str();
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  file.flush();
  if (!file) {
    error = "unable to write calibration manifest";
    return false;
  }
  return true;
}

}  // namespace

std::optional<SourceGuidedCalibrationResult>
render_source_guided_calibration_pair(
    amt::codec::ICodecService& codecs,
    const SourceGuidedCalibrationRequest& request,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (request.source_path.empty() || request.output_directory.empty() ||
      request.registry_path.empty() || request.worker_executable.empty() ||
      request.model_store_root.empty()) {
    error = "source-guided calibration request is missing a required path";
    return std::nullopt;
  }
  if (cancelled(cancellation)) {
    error = "source-guided calibration cancelled";
    return std::nullopt;
  }

  std::error_code directory_error;
  std::filesystem::create_directories(request.output_directory, directory_error);
  if (directory_error) {
    error = "unable to create calibration output directory: " +
            directory_error.message();
    return std::nullopt;
  }

  auto source_analysis = amt::analysis::analyze_file(
      codecs, request.source_path, error, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, value * 0.12);
      });
  if (!source_analysis) return std::nullopt;

  const auto base_plan = amt::mastering::plan_mastering(*source_analysis);
  auto stereo = amt::mastering::render_mastering_plan(
      codecs, request.source_path, request.output_directory / "stereo",
      *source_analysis, base_plan, error, request.render_settings, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, 0.12 + value * 0.18);
      });
  if (!stereo) return std::nullopt;

  std::string registry_error;
  auto selection = amt::separation::load_model_registry_selection(
      request.registry_path, request.worker_executable, registry_error,
      request.output_directory / "source-estimates");
  if (!selection) {
    error = "unable to load calibration model registry: " + registry_error;
    return std::nullopt;
  }
  if (!selection->active_separation_model) {
    error = "calibration requires an active separation model";
    return std::nullopt;
  }

  auto provider_config = *selection->active_separation_model;
  provider_config.model_artifact = resolve_model_artifact(
      request, provider_config.model_artifact, error);
  if (provider_config.model_artifact.empty()) return std::nullopt;

  auto install = amt::separation::ensure_model_artifact_installed(
      provider_config, error, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, 0.30 + value * 0.18);
      });
  if (!install) return std::nullopt;

  amt::separation::WorkerSeparationProvider provider(provider_config);
  amt::separation::SeparationCache cache(
      request.output_directory / "separation-cache");
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider, nullptr, &cache);

  amt::separation::SourceGuidanceRequest guidance_request;
  guidance_request.separation.source_path = request.source_path;
  guidance_request.separation.requested_stems = provider.model_manifest().stem_taxonomy;
  guidance_request.separation.request_stem_audio = true;
  guidance_request.separation.request_time_frequency_masks = false;

  amt::separation::SourceGuidedWorkflowConfig workflow_config;
  // Calibration is a private local evaluation path; it does not redistribute
  // the downloaded model artifact.
  workflow_config.guidance.require_bundled_production_model_eligibility = false;
  workflow_config.guidance.enable_cache = true;
  workflow_config.guidance.compute_missing_source_fingerprint = true;

  auto workflow = amt::separation::evaluate_source_guided_workflow(
      orchestrator, codecs, *source_analysis, std::move(guidance_request), error,
      workflow_config, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, 0.48 + value * 0.30);
      });
  if (!workflow) return std::nullopt;

  SourceGuidedCalibrationResult result;
  result.source_estimates_analyzed = workflow->source_estimates_analyzed;
  result.separation_provider_invoked = workflow->guidance.provider_invoked;
  result.separation_cache_hit = workflow->guidance.cache_hit;
  result.evidence_mode = workflow->guidance.decision.mode;
  result.stereo_master_a = stereo->master_a.output_path;
  result.model_name = provider_config.manifest.model_name;
  result.model_version = provider_config.manifest.model_version;
  result.issues = workflow->issues;
  result.warnings = selection->warnings;
  result.warnings.insert(result.warnings.end(), workflow->warnings.begin(),
                         workflow->warnings.end());
  result.manifest_path = request.output_directory / "calibration-manifest.json";

  std::optional<SourceGuidedMasteringRenderPair> guided;
  if (workflow->guidance.decision.mode ==
          amt::separation::SeparationMode::source_guided_stereo &&
      !workflow->issues.empty()) {
    guided = amt::mastering::render_mastering_plan_with_source_guidance(
        codecs, request.source_path, request.output_directory / "guided",
        *source_analysis, base_plan, workflow->guidance, workflow->issues, error,
        {}, request.render_settings, cancellation,
        [&](const double value) {
          amt::core::report_progress(progress, 0.78 + value * 0.18);
        });
    if (!guided) return std::nullopt;
    if (guided->source_guidance_applied) {
      result.guided_candidate_rendered = true;
      result.guided_master_a = guided->masters.master_a.output_path;
      if (!make_level_matched_auditions(
              codecs, *stereo, *guided, result, request.output_directory,
              error, cancellation)) {
        return std::nullopt;
      }
      amt::core::report_progress(progress, 0.99);
    } else {
      result.warnings.emplace_back(
          "evidence selected Mode 1, but the calibration render safely fell back to stereo");
    }
  } else {
    result.warnings.emplace_back(
        "source evidence did not qualify this track for a Mode 1 calibration candidate");
  }

  if (!write_manifest(request, result, guided ? &*guided : nullptr, *stereo, error)) {
    return std::nullopt;
  }
  amt::core::report_progress(progress, 1.0);
  return result;
}

}  // namespace amt::mastering
