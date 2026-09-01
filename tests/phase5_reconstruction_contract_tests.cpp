#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "amt/separation/Separation.h"
#include "amt/separation/SourceGuidance.h"

namespace {

amt::separation::SeparationModelManifest eligible_model() {
  amt::separation::SeparationModelManifest manifest;
  manifest.model_name = "complete-reconstruction-test";
  manifest.model_version = "1";
  manifest.model_sha256 = std::string(64U, 'c');
  manifest.architecture_source = "synthetic test architecture";
  manifest.weight_provenance = "synthetic test weights";
  manifest.code_license = "MIT";
  manifest.weights_license = "Synthetic test license";
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
  manifest.benchmark_record = "synthetic";
  manifest.security_reviewed = true;
  return manifest;
}

class ReconstructionProvider final : public amt::separation::ISeparationProvider {
 public:
  bool available() const noexcept override { return true; }
  amt::separation::SeparationModelManifest model_manifest() const override {
    return eligible_model();
  }

  std::optional<amt::separation::SeparationResult> separate(
      const amt::separation::SeparationRequest& request,
      std::string& error,
      const amt::core::CancellationToken*,
      const amt::core::ProgressCallback& progress) override {
    ++calls;
    error.clear();
    const auto root = request.cache_directory.empty()
        ? request.source_path.parent_path() / "synthetic-separation"
        : request.cache_directory;
    std::filesystem::create_directories(root / "stems");

    amt::separation::SeparationResult result;
    result.model = eligible_model();
    result.sample_rate = 48000;
    result.frames = 4096;
    result.overall_confidence = 0.95;
    result.complete_reconstruction = complete_reconstruction;

    const auto bass = root / "stems" / "bass.wav";
    std::ofstream(bass, std::ios::binary | std::ios::trunc) << "synthetic bass";
    result.artifacts.push_back({.kind = amt::separation::CacheArtifactKind::stem_audio,
                                .role = amt::separation::StemRole::bass,
                                .path = bass,
                                .confidence = 0.94});

    if (complete_reconstruction) {
      const auto other = root / "stems" / "other.wav";
      std::ofstream(other, std::ios::binary | std::ios::trunc) << "synthetic other";
      result.artifacts.push_back({.kind = amt::separation::CacheArtifactKind::stem_audio,
                                  .role = amt::separation::StemRole::other,
                                  .path = other,
                                  .confidence = 0.93});
    }
    amt::core::report_progress(progress, 1.0);
    return result;
  }

  bool complete_reconstruction{false};
  int calls{0};
};

class CountingArtifactEvaluator final
    : public amt::separation::IReconstructionArtifactEvaluator {
 public:
  std::optional<amt::separation::ArtifactAssessment> evaluate(
      const std::filesystem::path&,
      const amt::separation::SeparationResult& separation,
      std::string& error,
      const amt::core::CancellationToken*,
      const amt::core::ProgressCallback& progress) override {
    ++calls;
    error.clear();
    assert(separation.complete_reconstruction);
    amt::core::report_progress(progress, 1.0);
    return amt::separation::ArtifactAssessment{
        .overall_risk = 0.04,
        .confidence = 0.95,
        .evidence = {"synthetic reconstruction is clean"}};
  }

  int calls{0};
};

amt::separation::SourceGuidanceRequest reconstruction_request(
    const std::filesystem::path& source) {
  amt::separation::SourceGuidanceRequest request;
  request.separation.source_path = source;
  request.separation.requested_stems = {amt::separation::StemRole::vocals,
                                        amt::separation::StemRole::drums,
                                        amt::separation::StemRole::bass,
                                        amt::separation::StemRole::other};
  request.separation.request_stem_audio = true;
  request.separation.request_time_frequency_masks = false;
  request.cache_key.source_fingerprint = std::string(64U, 'f');
  request.evidence = {.source_specific_issue = true,
                      .reconstruction_required_for_full_repair = true,
                      .expected_repair_benefit = 0.84,
                      .source_guidance_confidence = 0.90,
                      .source_guided_stereo_sufficiency = 0.72,
                      .model_confidence = 0.0};
  return request;
}

void test_partial_stems_cannot_enter_mode2() {
  const auto root = std::filesystem::temp_directory_path() /
                    "amt-phase5-partial-reconstruction-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  const auto source = root / "source.wav";
  std::ofstream(source, std::ios::binary | std::ios::trunc) << "source";

  ReconstructionProvider provider;
  CountingArtifactEvaluator evaluator;
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider, &evaluator, nullptr);
  auto request = reconstruction_request(source);
  amt::separation::SourceGuidanceConfig config;
  config.enable_cache = false;

  std::string error;
  const auto partial = orchestrator.execute(request, error, config);
  assert(partial.has_value());
  assert(error.empty());
  assert(partial->decision.mode == amt::separation::SeparationMode::source_guided_stereo);
  assert(partial->separation.has_value());
  assert(!partial->separation->complete_reconstruction);
  assert(evaluator.calls == 0);
  assert(!partial->warnings.empty());

  provider.complete_reconstruction = true;
  const auto complete = orchestrator.execute(request, error, config);
  assert(complete.has_value());
  assert(error.empty());
  assert(complete->decision.mode == amt::separation::SeparationMode::stem_reconstruction);
  assert(complete->separation->complete_reconstruction);
  assert(evaluator.calls == 1);

  std::filesystem::remove_all(root, ignored);
}

void test_complete_reconstruction_survives_cache_replay() {
  const auto root = std::filesystem::temp_directory_path() /
                    "amt-phase5-complete-reconstruction-cache-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  const auto source = root / "source.wav";
  std::ofstream(source, std::ios::binary | std::ios::trunc) << "source";

  ReconstructionProvider provider;
  provider.complete_reconstruction = true;
  CountingArtifactEvaluator evaluator;
  amt::separation::SeparationCache cache(root / "cache");
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider, &evaluator, &cache);
  const auto request = reconstruction_request(source);

  std::string error;
  const auto first = orchestrator.execute(request, error);
  assert(first.has_value());
  assert(error.empty());
  assert(first->decision.mode == amt::separation::SeparationMode::stem_reconstruction);
  assert(first->provider_invoked);
  assert(!first->cache_hit);
  assert(first->separation->complete_reconstruction);
  assert(provider.calls == 1);
  assert(evaluator.calls == 1);

  const auto second = orchestrator.execute(request, error);
  assert(second.has_value());
  assert(error.empty());
  assert(second->decision.mode == amt::separation::SeparationMode::stem_reconstruction);
  assert(!second->provider_invoked);
  assert(second->cache_hit);
  assert(second->separation->complete_reconstruction);
  assert(provider.calls == 1);
  assert(evaluator.calls == 2);

  std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main() {
  test_partial_stems_cannot_enter_mode2();
  test_complete_reconstruction_survives_cache_replay();
  return 0;
}
