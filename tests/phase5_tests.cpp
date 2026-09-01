#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "amt/separation/Separation.h"
#include "amt/separation/SourceGuidance.h"

namespace {

amt::separation::SeparationModelManifest valid_model_manifest() {
  amt::separation::SeparationModelManifest manifest;
  manifest.model_name = "test-separator";
  manifest.model_version = "1.0.0";
  manifest.model_sha256 = std::string(64U, 'a');
  manifest.architecture_source = "owned synthetic test architecture";
  manifest.weight_provenance = "owned synthetic test weights";
  manifest.code_license = "MIT";
  manifest.weights_license = "Internal test license";
  manifest.redistribution_reviewed = true;
  manifest.redistribution_allowed = true;
  manifest.commercial_use_reviewed = true;
  manifest.commercial_use_allowed = true;
  manifest.supported_execution_providers = {"CPU"};
  manifest.expected_input_sample_rate = 48000;
  manifest.stem_taxonomy = {amt::separation::StemRole::vocals,
                            amt::separation::StemRole::drums,
                            amt::separation::StemRole::bass,
                            amt::separation::StemRole::other};
  manifest.benchmark_record = "synthetic-test-v1";
  manifest.security_reviewed = true;
  return manifest;
}

class FakeProvider final : public amt::separation::ISeparationProvider {
 public:
  bool available() const noexcept override { return available_; }
  amt::separation::SeparationModelManifest model_manifest() const override { return model_; }

  std::optional<amt::separation::SeparationResult> separate(
      const amt::separation::SeparationRequest& request, std::string& error,
      const amt::core::CancellationToken* cancellation,
      const amt::core::ProgressCallback& progress) override {
    ++calls_;
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "cancelled";
      return std::nullopt;
    }
    if (fail_) {
      error = "synthetic provider failure";
      return std::nullopt;
    }

    const auto output_root = request.cache_directory.empty()
        ? request.source_path.parent_path() / "synthetic-separation"
        : request.cache_directory;
    std::filesystem::create_directories(output_root / "stems");
    std::filesystem::create_directories(output_root / "masks");

    amt::separation::SeparationResult result;
    result.model = model_;
    result.sample_rate = 48000;
    result.frames = 4096;
    result.overall_confidence = confidence_;
    result.complete_reconstruction = complete_reconstruction_;

    if (return_source_as_artifact_) {
      result.artifacts.push_back({.kind = amt::separation::CacheArtifactKind::stem_audio,
                                  .role = amt::separation::StemRole::bass,
                                  .path = request.source_path,
                                  .confidence = confidence_});
      return result;
    }

    if (emit_stems_) {
      const auto stem_path = output_root / "stems" / "bass.wav";
      std::ofstream(stem_path, std::ios::binary) << "synthetic stem";
      result.artifacts.push_back({.kind = amt::separation::CacheArtifactKind::stem_audio,
                                  .role = amt::separation::StemRole::bass,
                                  .path = stem_path,
                                  .confidence = confidence_});
    }
    if (emit_masks_) {
      const auto mask_path = output_root / "masks" / "bass.mask";
      std::ofstream(mask_path, std::ios::binary) << "synthetic mask";
      result.artifacts.push_back({.kind = amt::separation::CacheArtifactKind::time_frequency_mask,
                                  .role = amt::separation::StemRole::bass,
                                  .path = mask_path,
                                  .confidence = confidence_});
    }
    amt::core::report_progress(progress, 1.0);
    return result;
  }

  amt::separation::SeparationModelManifest model_{valid_model_manifest()};
  bool available_{true};
  bool fail_{false};
  bool return_source_as_artifact_{false};
  bool emit_stems_{true};
  bool emit_masks_{true};
  bool complete_reconstruction_{true};
  double confidence_{0.95};
  int calls_{0};
};

class FakeArtifactEvaluator final : public amt::separation::IReconstructionArtifactEvaluator {
 public:
  std::optional<amt::separation::ArtifactAssessment> evaluate(
      const std::filesystem::path&,
      const amt::separation::SeparationResult&,
      std::string& error,
      const amt::core::CancellationToken* cancellation,
      const amt::core::ProgressCallback& progress) override {
    ++calls_;
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "cancelled";
      return std::nullopt;
    }
    if (fail_) {
      error = "synthetic artifact evaluation failure";
      return std::nullopt;
    }
    amt::core::report_progress(progress, 1.0);
    return assessment_;
  }

  amt::separation::ArtifactAssessment assessment_{
      .overall_risk = 0.05,
      .confidence = 0.95,
      .evidence = {"synthetic low artifact result"}};
  bool fail_{false};
  int calls_{0};
};

void test_model_manifest_gate() {
  auto manifest = valid_model_manifest();
  const auto accepted = amt::separation::evaluate_model_for_bundled_production(manifest);
  assert(accepted.eligible_for_bundled_production);
  assert(accepted.blockers.empty());

  manifest.weights_license.clear();
  const auto missing_weight_license =
      amt::separation::evaluate_model_for_bundled_production(manifest);
  assert(!missing_weight_license.eligible_for_bundled_production);
  assert(!missing_weight_license.blockers.empty());

  manifest = valid_model_manifest();
  manifest.commercial_use_allowed = false;
  const auto noncommercial = amt::separation::evaluate_model_for_bundled_production(manifest);
  assert(!noncommercial.eligible_for_bundled_production);

  manifest = valid_model_manifest();
  manifest.model_sha256 = "not-a-sha";
  const auto bad_hash = amt::separation::evaluate_model_for_bundled_production(manifest);
  assert(!bad_hash.eligible_for_bundled_production);
}

void test_artifact_assessment() {
  const auto clean = amt::separation::assess_reconstruction_artifacts(
      {.leakage = 0.04,
       .musical_noise = 0.03,
       .transient_damage = 0.05,
       .phase_change = 0.03,
       .high_frequency_smearing = 0.04,
       .source_residue = 0.04,
       .reconstruction_residual = 0.05,
       .model_confidence = 0.95,
       .measurement_confidence = 0.90});
  assert(clean.overall_risk < 0.15);
  assert(clean.confidence > 0.85);

  const auto damaged = amt::separation::assess_reconstruction_artifacts(
      {.leakage = 0.12,
       .musical_noise = 0.20,
       .transient_damage = 0.82,
       .phase_change = 0.30,
       .high_frequency_smearing = 0.45,
       .source_residue = 0.18,
       .reconstruction_residual = 0.22,
       .model_confidence = 0.92,
       .measurement_confidence = 0.90});
  assert(damaged.overall_risk > 0.60);
  assert(!damaged.evidence.empty());
}

void test_mode_policy() {
  const auto low_artifact = amt::separation::assess_reconstruction_artifacts(
      {.leakage = 0.03,
       .musical_noise = 0.03,
       .transient_damage = 0.04,
       .phase_change = 0.03,
       .high_frequency_smearing = 0.03,
       .source_residue = 0.04,
       .reconstruction_residual = 0.04,
       .model_confidence = 0.95,
       .measurement_confidence = 0.95});

  const auto no_issue = amt::separation::choose_separation_mode(
      {.source_specific_issue = false,
       .expected_repair_benefit = 0.9,
       .source_guidance_confidence = 0.9,
       .source_guided_stereo_sufficiency = 0.9,
       .model_confidence = 0.95},
      low_artifact);
  assert(no_issue.mode == amt::separation::SeparationMode::stereo_mastering);

  const auto guided = amt::separation::choose_separation_mode(
      {.source_specific_issue = true,
       .reconstruction_required_for_full_repair = false,
       .expected_repair_benefit = 0.65,
       .source_guidance_confidence = 0.86,
       .source_guided_stereo_sufficiency = 0.78,
       .model_confidence = 0.92},
      low_artifact);
  assert(guided.mode == amt::separation::SeparationMode::source_guided_stereo);

  const auto reconstructed = amt::separation::choose_separation_mode(
      {.source_specific_issue = true,
       .reconstruction_required_for_full_repair = true,
       .expected_repair_benefit = 0.84,
       .source_guidance_confidence = 0.90,
       .source_guided_stereo_sufficiency = 0.70,
       .model_confidence = 0.95},
      low_artifact);
  assert(reconstructed.mode == amt::separation::SeparationMode::stem_reconstruction);

  const auto high_artifact = amt::separation::assess_reconstruction_artifacts(
      {.leakage = 0.48,
       .musical_noise = 0.55,
       .transient_damage = 0.70,
       .phase_change = 0.35,
       .high_frequency_smearing = 0.58,
       .source_residue = 0.45,
       .reconstruction_residual = 0.40,
       .model_confidence = 0.95,
       .measurement_confidence = 0.95});
  const auto safe_fallback = amt::separation::choose_separation_mode(
      {.source_specific_issue = true,
       .reconstruction_required_for_full_repair = true,
       .expected_repair_benefit = 0.85,
       .source_guidance_confidence = 0.88,
       .source_guided_stereo_sufficiency = 0.72,
       .model_confidence = 0.95},
      high_artifact);
  assert(safe_fallback.mode == amt::separation::SeparationMode::source_guided_stereo);

  const auto insufficient_guidance = amt::separation::choose_separation_mode(
      {.source_specific_issue = true,
       .reconstruction_required_for_full_repair = true,
       .expected_repair_benefit = 0.80,
       .source_guidance_confidence = 0.30,
       .source_guided_stereo_sufficiency = 0.25,
       .model_confidence = 0.60},
      high_artifact);
  assert(insufficient_guidance.mode == amt::separation::SeparationMode::stereo_mastering);
}

void test_cache_key_and_manifest() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase5-separation-cache-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  amt::separation::SeparationCacheKey key;
  key.source_fingerprint = std::string(64U, '1');
  key.model_name = "separator";
  key.model_version = "2";
  key.model_sha256 = std::string(64U, '2');
  key.requested_stems = {amt::separation::StemRole::vocals,
                         amt::separation::StemRole::drums};
  key.stem_audio = true;
  key.masks = true;

  auto reordered = key;
  reordered.requested_stems = {amt::separation::StemRole::drums,
                               amt::separation::StemRole::vocals,
                               amt::separation::StemRole::drums};
  assert(amt::separation::canonical_cache_key(key) ==
         amt::separation::canonical_cache_key(reordered));
  assert(amt::separation::stable_cache_id(key) ==
         amt::separation::stable_cache_id(reordered));

  amt::separation::SeparationCache cache(root);
  std::string error;
  assert(cache.prepare(key, error));
  const auto directory = cache.entry_directory(key);
  std::filesystem::create_directories(directory / "stems");
  std::filesystem::create_directories(directory / "masks");
  {
    std::ofstream(directory / "stems" / "vocals.wav", std::ios::binary) << "stem";
    std::ofstream(directory / "masks" / "drums.bin", std::ios::binary) << "mask";
  }

  amt::separation::SeparationCacheEntry entry;
  entry.key = key;
  entry.sample_rate = 48000;
  entry.frames = 12345;
  entry.overall_confidence = 0.91;
  entry.artifacts = {
      {.kind = amt::separation::CacheArtifactKind::stem_audio,
       .role = amt::separation::StemRole::vocals,
       .relative_path = std::filesystem::path("stems") / "vocals.wav",
       .confidence = 0.92},
      {.kind = amt::separation::CacheArtifactKind::time_frequency_mask,
       .role = amt::separation::StemRole::drums,
       .relative_path = std::filesystem::path("masks") / "drums.bin",
       .confidence = 0.89}};
  assert(cache.mark_complete(entry, error));

  const auto loaded = cache.load_complete(reordered, error);
  assert(loaded.has_value());
  assert(loaded->sample_rate == 48000);
  assert(loaded->frames == 12345);
  assert(loaded->overall_confidence == 0.91);
  assert(loaded->artifacts.size() == 2U);
  assert(loaded->artifacts.front().confidence == 0.92);
  assert(error.empty());

  std::filesystem::remove(directory / "masks" / "drums.bin", ignored);
  const auto incomplete = cache.load_complete(key, error);
  assert(!incomplete.has_value());
  assert(error.empty());

  entry.artifacts = {{.kind = amt::separation::CacheArtifactKind::features,
                      .role = amt::separation::StemRole::unknown,
                      .relative_path = "../escape.bin",
                      .confidence = 0.5}};
  assert(!cache.mark_complete(entry, error));
  assert(!error.empty());

  std::filesystem::remove_all(root, ignored);
}

amt::separation::SourceGuidanceRequest source_guidance_request(
    const std::filesystem::path& source) {
  amt::separation::SourceGuidanceRequest request;
  request.separation.source_path = source;
  request.separation.requested_stems = {amt::separation::StemRole::bass};
  request.separation.request_stem_audio = true;
  request.separation.request_time_frequency_masks = true;
  request.cache_key.source_fingerprint = std::string(64U, 'f');
  request.evidence = {.source_specific_issue = true,
                      .reconstruction_required_for_full_repair = true,
                      .expected_repair_benefit = 0.85,
                      .source_guidance_confidence = 0.90,
                      .source_guided_stereo_sufficiency = 0.75,
                      .model_confidence = 0.0};
  return request;
}

void test_orchestrator_mode0_skips_provider() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase5-mode0-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  const auto source = root / "source.wav";
  std::ofstream(source, std::ios::binary) << "source";

  FakeProvider provider;
  FakeArtifactEvaluator evaluator;
  amt::separation::SeparationCache cache(root / "cache");
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider, &evaluator, &cache);
  auto request = source_guidance_request(source);
  request.evidence.source_specific_issue = false;

  std::string error;
  const auto result = orchestrator.execute(request, error);
  assert(result.has_value());
  assert(result->decision.mode == amt::separation::SeparationMode::stereo_mastering);
  assert(!result->provider_invoked);
  assert(!result->cache_hit);
  assert(provider.calls_ == 0);
  assert(evaluator.calls_ == 0);
  assert(error.empty());
  std::filesystem::remove_all(root, ignored);
}

void test_orchestrator_model_gate_and_safe_fallbacks() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase5-gate-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  const auto source = root / "source.wav";
  std::ofstream(source, std::ios::binary) << "source";

  FakeProvider provider;
  provider.model_.weights_license.clear();
  FakeArtifactEvaluator evaluator;
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider, &evaluator, nullptr);
  const auto request = source_guidance_request(source);
  std::string error;
  const auto blocked = orchestrator.execute(request, error);
  assert(blocked.has_value());
  assert(blocked->decision.mode == amt::separation::SeparationMode::stereo_mastering);
  assert(provider.calls_ == 0);
  assert(!blocked->warnings.empty());

  provider.model_ = valid_model_manifest();
  provider.return_source_as_artifact_ = true;
  const auto unsafe = orchestrator.execute(request, error);
  assert(unsafe.has_value());
  assert(unsafe->decision.mode == amt::separation::SeparationMode::stereo_mastering);
  assert(provider.calls_ == 1);
  assert(!unsafe->warnings.empty());

  std::filesystem::remove_all(root, ignored);
}

void test_orchestrator_mode2_and_artifact_fallback() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase5-orchestrator-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  const auto source = root / "source.wav";
  std::ofstream(source, std::ios::binary) << "source";

  FakeProvider provider;
  FakeArtifactEvaluator evaluator;
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider, &evaluator, nullptr);
  auto request = source_guidance_request(source);
  amt::separation::SourceGuidanceConfig config;
  config.enable_cache = false;

  std::string error;
  const auto reconstruction = orchestrator.execute(request, error, config);
  assert(reconstruction.has_value());
  assert(reconstruction->decision.mode == amt::separation::SeparationMode::stem_reconstruction);
  assert(reconstruction->provider_invoked);
  assert(provider.calls_ == 1);
  assert(evaluator.calls_ == 1);

  evaluator.assessment_.overall_risk = 0.80;
  evaluator.assessment_.confidence = 0.95;
  const auto fallback = orchestrator.execute(request, error, config);
  assert(fallback.has_value());
  assert(fallback->decision.mode == amt::separation::SeparationMode::source_guided_stereo);
  assert(provider.calls_ == 2);
  assert(evaluator.calls_ == 2);

  request.evidence.reconstruction_required_for_full_repair = false;
  evaluator.assessment_.overall_risk = 0.05;
  const auto guided = orchestrator.execute(request, error, config);
  assert(guided.has_value());
  assert(guided->decision.mode == amt::separation::SeparationMode::source_guided_stereo);

  std::filesystem::remove_all(root, ignored);
}

void test_orchestrator_cache_replay() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase5-orchestrator-cache-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  const auto source = root / "source.wav";
  std::ofstream(source, std::ios::binary) << "source";

  FakeProvider provider;
  FakeArtifactEvaluator evaluator;
  amt::separation::SeparationCache cache(root / "cache");
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider, &evaluator, &cache);
  const auto request = source_guidance_request(source);

  std::string error;
  const auto first = orchestrator.execute(request, error);
  assert(first.has_value());
  assert(first->provider_invoked);
  assert(!first->cache_hit);
  assert(provider.calls_ == 1);

  const auto second = orchestrator.execute(request, error);
  assert(second.has_value());
  assert(!second->provider_invoked);
  assert(second->cache_hit);
  assert(second->separation.has_value());
  assert(second->separation->overall_confidence == provider.confidence_);
  assert(provider.calls_ == 1);

  std::filesystem::remove_all(root, ignored);
}

void test_names() {
  assert(amt::separation::stem_role_from_name("kick") == amt::separation::StemRole::kick);
  assert(amt::separation::stem_role_name(amt::separation::StemRole::bass) == "bass");
  assert(amt::separation::separation_mode_name(
             amt::separation::SeparationMode::source_guided_stereo) ==
         "source_guided_stereo");
}

}  // namespace

int main() {
  test_model_manifest_gate();
  test_artifact_assessment();
  test_mode_policy();
  test_cache_key_and_manifest();
  test_orchestrator_mode0_skips_provider();
  test_orchestrator_model_gate_and_safe_fallbacks();
  test_orchestrator_mode2_and_artifact_fallback();
  test_orchestrator_cache_replay();
  test_names();
  return 0;
}
