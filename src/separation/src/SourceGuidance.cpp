#include "amt/separation/SourceGuidance.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "amt/core/FileFingerprint.h"

namespace amt::separation {
namespace {

[[nodiscard]] double clamp01(const double value) {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] bool cache_relative_path_is_safe(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& component : path) {
    if (component == "..") return false;
  }
  return true;
}

[[nodiscard]] bool paths_refer_to_same_file(const std::filesystem::path& first,
                                            const std::filesystem::path& second) {
  std::error_code first_exists_error;
  const bool first_exists = std::filesystem::exists(first, first_exists_error);
  std::error_code second_exists_error;
  const bool second_exists = std::filesystem::exists(second, second_exists_error);
  if (!first_exists_error && !second_exists_error && first_exists && second_exists) {
    std::error_code equivalent_error;
    const bool equivalent = std::filesystem::equivalent(first, second, equivalent_error);
    if (!equivalent_error) return equivalent;
  }

  std::error_code first_absolute_error;
  const auto first_absolute = std::filesystem::absolute(first, first_absolute_error).lexically_normal();
  std::error_code second_absolute_error;
  const auto second_absolute = std::filesystem::absolute(second, second_absolute_error).lexically_normal();
  return !first_absolute_error && !second_absolute_error && first_absolute == second_absolute;
}

[[nodiscard]] bool has_artifacts(const SeparationResult& result) {
  return !result.artifacts.empty();
}

[[nodiscard]] bool has_stem_audio(const SeparationResult& result) {
  return std::any_of(result.artifacts.begin(), result.artifacts.end(), [](const auto& artifact) {
    return artifact.kind == CacheArtifactKind::stem_audio;
  });
}

[[nodiscard]] SeparationDecision stereo_fallback(const SourceInterventionEvidence& evidence,
                                                 std::string reason) {
  SeparationDecision decision;
  decision.mode = SeparationMode::stereo_mastering;
  decision.expected_benefit = clamp01(evidence.expected_repair_benefit);
  decision.artifact_risk = 1.0;
  decision.confidence = 1.0;
  decision.reasons.push_back(std::move(reason));
  return decision;
}

[[nodiscard]] bool model_identity_matches(const SeparationModelManifest& expected,
                                          const SeparationModelManifest& actual) {
  return expected.model_name == actual.model_name &&
         expected.model_version == actual.model_version &&
         expected.model_sha256 == actual.model_sha256;
}

[[nodiscard]] SeparationCacheKey cache_key_for(const SourceGuidanceRequest& request,
                                               const SeparationModelManifest& model) {
  auto key = request.cache_key;
  key.model_name = model.model_name;
  key.model_version = model.model_version;
  key.model_sha256 = model.model_sha256;
  key.requested_stems = request.separation.requested_stems;
  key.stem_audio = request.separation.request_stem_audio;
  key.masks = request.separation.request_time_frequency_masks;
  return key;
}

[[nodiscard]] SeparationResult separation_from_cache(
    const SeparationCacheEntry& entry,
    const SeparationModelManifest& model,
    const std::filesystem::path& directory) {
  SeparationResult result;
  result.model = model;
  result.sample_rate = entry.sample_rate;
  result.frames = entry.frames;
  result.overall_confidence = entry.overall_confidence;
  result.artifacts.reserve(entry.artifacts.size());
  for (const auto& artifact : entry.artifacts) {
    result.artifacts.push_back({.kind = artifact.kind,
                                .role = artifact.role,
                                .path = directory / artifact.relative_path,
                                .confidence = artifact.confidence});
  }
  return result;
}

bool validate_provider_result(SeparationResult& result,
                              const SeparationModelManifest& expected_model,
                              const std::filesystem::path& original_source,
                              const std::filesystem::path& cache_directory,
                              std::string& error) {
  if (!model_identity_matches(expected_model, result.model)) {
    error = "separation provider returned a different model identity than it declared";
    return false;
  }
  if (result.sample_rate <= 0 || result.frames < 0 ||
      !std::isfinite(result.overall_confidence) ||
      result.overall_confidence < 0.0 || result.overall_confidence > 1.0) {
    error = "separation provider returned invalid result metadata";
    return false;
  }
  if (result.artifacts.empty()) {
    error = "separation provider returned no source estimates";
    return false;
  }

  for (auto& artifact : result.artifacts) {
    if (!std::isfinite(artifact.confidence) || artifact.confidence < 0.0 ||
        artifact.confidence > 1.0 || artifact.path.empty()) {
      error = "separation provider returned invalid artifact metadata";
      return false;
    }
    if (artifact.path.is_relative() && !cache_directory.empty()) {
      artifact.path = cache_directory / artifact.path;
    }
    if (paths_refer_to_same_file(artifact.path, original_source)) {
      error = "separation provider attempted to use the canonical source path as an output artifact";
      return false;
    }
  }
  return true;
}

bool persist_cache_entry(SeparationCache& cache,
                         const SeparationCacheKey& key,
                         const SeparationResult& result,
                         std::string& warning) {
  const auto directory = cache.entry_directory(key);
  SeparationCacheEntry entry;
  entry.key = key;
  entry.sample_rate = result.sample_rate;
  entry.frames = result.frames;
  entry.overall_confidence = result.overall_confidence;
  entry.artifacts.reserve(result.artifacts.size());

  for (const auto& artifact : result.artifacts) {
    std::error_code relative_error;
    const auto relative = std::filesystem::relative(artifact.path, directory, relative_error);
    if (relative_error || !cache_relative_path_is_safe(relative)) {
      warning = "separation completed, but at least one provider artifact was outside the managed cache";
      return false;
    }
    entry.artifacts.push_back({.kind = artifact.kind,
                               .role = artifact.role,
                               .relative_path = relative,
                               .confidence = artifact.confidence});
  }

  std::string cache_error;
  if (!cache.mark_complete(entry, cache_error)) {
    warning = "separation completed, but its cache manifest could not be committed: " + cache_error;
    return false;
  }
  return true;
}

}  // namespace

std::optional<SourceGuidanceResult> SourceGuidanceOrchestrator::execute(
    const SourceGuidanceRequest& request,
    std::string& error,
    const SourceGuidanceConfig& config,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  SourceGuidanceResult output;

  if (request.separation.source_path.empty()) {
    error = "source-guidance request is missing its canonical source path";
    return std::nullopt;
  }
  if (cancellation != nullptr && cancellation->is_cancelled()) {
    error = "source-guidance evaluation cancelled";
    return std::nullopt;
  }

  ArtifactAssessment unknown_artifacts;
  unknown_artifacts.overall_risk = 1.0;
  unknown_artifacts.confidence = 0.0;
  unknown_artifacts.evidence = {"reconstruction has not been evaluated"};

  const double minimum_benefit = clamp01(config.policy.minimum_expected_benefit);
  const double guidance_confidence = clamp01(request.evidence.source_guidance_confidence);
  const bool guidance_cannot_qualify =
      !request.evidence.reconstruction_required_for_full_repair &&
      guidance_confidence < clamp01(config.policy.minimum_guidance_confidence);
  if (!request.evidence.source_specific_issue ||
      clamp01(request.evidence.expected_repair_benefit) < minimum_benefit ||
      guidance_cannot_qualify) {
    output.decision = choose_separation_mode(request.evidence, unknown_artifacts, config.policy);
    amt::core::report_progress(progress, 1.0);
    return output;
  }

  if (!provider_.available()) {
    output.decision = stereo_fallback(request.evidence,
                                      "source-separation provider is unavailable; preserving the original stereo mix");
    output.warnings.emplace_back("source-specific processing was requested but no separation provider is available");
    amt::core::report_progress(progress, 1.0);
    return output;
  }

  const auto model = provider_.model_manifest();
  if (config.require_bundled_production_model_eligibility) {
    const auto eligibility = evaluate_model_for_bundled_production(model);
    if (!eligibility.eligible_for_bundled_production) {
      output.decision = stereo_fallback(request.evidence,
                                        "the configured separation model is not approved for bundled production use");
      output.warnings.insert(output.warnings.end(), eligibility.blockers.begin(),
                             eligibility.blockers.end());
      amt::core::report_progress(progress, 1.0);
      return output;
    }
  }

  auto separation_request = request.separation;
  auto cache_key = cache_key_for(request, model);
  bool cache_enabled = config.enable_cache && cache_ != nullptr;
  if (cache_enabled && cache_key.source_fingerprint.empty()) {
    if (config.compute_missing_source_fingerprint) {
      std::string fingerprint_error;
      const auto fingerprint = amt::core::fingerprint_file_sha256(
          request.separation.source_path, fingerprint_error, cancellation,
          [&](const double value) { amt::core::report_progress(progress, value * 0.10); });
      if (fingerprint) {
        cache_key.source_fingerprint = fingerprint->sha256;
      } else if (cancellation != nullptr && cancellation->is_cancelled()) {
        error = "source-guidance evaluation cancelled";
        return std::nullopt;
      } else {
        cache_enabled = false;
        output.warnings.emplace_back("separation cache disabled because the source could not be fingerprinted: " +
                                     fingerprint_error);
      }
    } else {
      cache_enabled = false;
      output.warnings.emplace_back("separation cache skipped because no stable source fingerprint was supplied");
    }
  }

  if (cache_enabled) {
    std::string cache_error;
    const auto cached = cache_->load_complete(cache_key, cache_error);
    if (cached) {
      output.cache_hit = true;
      output.separation = separation_from_cache(*cached, model, cache_->entry_directory(cache_key));
      amt::core::report_progress(progress, 0.70);
    } else {
      if (!cache_error.empty()) {
        output.warnings.emplace_back("ignoring invalid separation cache entry: " + cache_error);
      }
      cache_error.clear();
      if (cache_->prepare(cache_key, cache_error)) {
        separation_request.cache_directory = cache_->entry_directory(cache_key);
      } else {
        cache_enabled = false;
        output.warnings.emplace_back("separation cache disabled for this run: " + cache_error);
      }
    }
  }

  if (!output.separation) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "source-guidance evaluation cancelled";
      return std::nullopt;
    }
    output.provider_invoked = true;
    std::string provider_error;
    auto separated = provider_.separate(
        separation_request, provider_error, cancellation,
        [&](const double value) { amt::core::report_progress(progress, 0.10 + value * 0.60); });
    if (!separated) {
      if (cancellation != nullptr && cancellation->is_cancelled()) {
        error = "source-guidance evaluation cancelled";
        return std::nullopt;
      }
      output.decision = stereo_fallback(request.evidence,
                                        "separation failed; preserving the original stereo mix");
      output.warnings.emplace_back(provider_error.empty()
                                       ? "separation provider failed without diagnostic detail"
                                       : provider_error);
      amt::core::report_progress(progress, 1.0);
      return output;
    }

    std::string validation_error;
    if (!validate_provider_result(*separated, model, request.separation.source_path,
                                  separation_request.cache_directory, validation_error)) {
      output.decision = stereo_fallback(request.evidence,
                                        "separation output failed safety validation; preserving the original stereo mix");
      output.warnings.emplace_back(std::move(validation_error));
      amt::core::report_progress(progress, 1.0);
      return output;
    }
    output.separation = std::move(*separated);

    if (cache_enabled) {
      std::string cache_warning;
      if (!persist_cache_entry(*cache_, cache_key, *output.separation, cache_warning) &&
          !cache_warning.empty()) {
        output.warnings.push_back(std::move(cache_warning));
      }
    }
  }

  if (cancellation != nullptr && cancellation->is_cancelled()) {
    error = "source-guidance evaluation cancelled";
    return std::nullopt;
  }

  ArtifactAssessment artifact_for_policy = unknown_artifacts;
  if (request.evidence.reconstruction_required_for_full_repair &&
      artifact_evaluator_ != nullptr && has_stem_audio(*output.separation)) {
    std::string artifact_error;
    const auto assessed = artifact_evaluator_->evaluate(
        request.separation.source_path, *output.separation, artifact_error, cancellation,
        [&](const double value) { amt::core::report_progress(progress, 0.70 + value * 0.30); });
    if (assessed) {
      artifact_for_policy = *assessed;
      output.artifact_assessment = *assessed;
    } else if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "source-guidance evaluation cancelled";
      return std::nullopt;
    } else {
      output.warnings.emplace_back(artifact_error.empty()
                                       ? "reconstruction artifact evaluation was unavailable"
                                       : artifact_error);
    }
  } else {
    if (request.evidence.reconstruction_required_for_full_repair) {
      output.warnings.emplace_back("reconstruction was requested but no validated stem-audio artifact assessment is available");
    }
    amt::core::report_progress(progress, 1.0);
  }

  auto effective_evidence = request.evidence;
  effective_evidence.model_confidence = clamp01(output.separation->overall_confidence);
  effective_evidence.source_guidance_confidence =
      std::min(clamp01(request.evidence.source_guidance_confidence),
               effective_evidence.model_confidence);
  output.decision = choose_separation_mode(effective_evidence, artifact_for_policy, config.policy);

  if (output.decision.mode == SeparationMode::stem_reconstruction &&
      !has_stem_audio(*output.separation)) {
    ArtifactAssessment reconstruction_blocked = artifact_for_policy;
    reconstruction_blocked.overall_risk = 1.0;
    reconstruction_blocked.confidence = 0.0;
    output.decision = choose_separation_mode(effective_evidence, reconstruction_blocked,
                                             config.policy);
    output.decision.reasons.emplace_back("stem reconstruction was blocked because no stem-audio outputs were available");
  }

  if (output.decision.mode == SeparationMode::source_guided_stereo &&
      !has_artifacts(*output.separation)) {
    output.decision = stereo_fallback(effective_evidence,
                                      "no usable source estimate is available; preserving the original stereo mix");
  }

  amt::core::report_progress(progress, 1.0);
  return output;
}

}  // namespace amt::separation
