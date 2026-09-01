#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "amt/core/FileFingerprint.h"
#include "amt/separation/SourceGuidance.h"

namespace {


std::filesystem::path write_file(const std::filesystem::path& path, const std::string& content) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.close();
  return path;
}

void test_standard_sha256_vectors() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase5-sha256-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);

  std::string error;
  const auto empty = amt::core::fingerprint_file_sha256(write_file(root / "empty.bin", ""), error);
  assert(empty.has_value());
  assert(error.empty());
  assert(empty->size_bytes == 0U);
  assert(empty->sha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  const auto abc = amt::core::fingerprint_file_sha256(write_file(root / "abc.bin", "abc"), error);
  assert(abc.has_value());
  assert(error.empty());
  assert(abc->size_bytes == 3U);
  assert(abc->sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  const auto quick = amt::core::fingerprint_file_sha256(
      write_file(root / "quick.bin", "The quick brown fox jumps over the lazy dog"), error);
  assert(quick.has_value());
  assert(error.empty());
  assert(quick->sha256 == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

  std::filesystem::remove_all(root, ignored);
}

amt::separation::SeparationModelManifest eligible_model() {
  amt::separation::SeparationModelManifest manifest;
  manifest.model_name = "fingerprint-cache-test";
  manifest.model_version = "1";
  manifest.model_sha256 = std::string(64U, 'a');
  manifest.architecture_source = "synthetic test provider";
  manifest.weight_provenance = "synthetic test weights";
  manifest.code_license = "MIT";
  manifest.weights_license = "Synthetic test license";
  manifest.redistribution_reviewed = true;
  manifest.redistribution_allowed = true;
  manifest.commercial_use_reviewed = true;
  manifest.commercial_use_allowed = true;
  manifest.supported_execution_providers = {"CPU"};
  manifest.expected_input_sample_rate = 48000;
  manifest.stem_taxonomy = {amt::separation::StemRole::bass};
  manifest.benchmark_record = "synthetic";
  manifest.security_reviewed = true;
  return manifest;
}

class CountingMaskProvider final : public amt::separation::ISeparationProvider {
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
    if (request.cache_directory.empty()) {
      error = "managed cache directory was not supplied";
      return std::nullopt;
    }

    std::error_code ec;
    std::filesystem::create_directories(request.cache_directory / "masks", ec);
    if (ec) {
      error = ec.message();
      return std::nullopt;
    }
    const auto mask_path = request.cache_directory / "masks" / "bass.mask";
    write_file(mask_path, "synthetic-mask-" + std::to_string(calls));

    amt::separation::SeparationResult result;
    result.model = eligible_model();
    result.sample_rate = 48000;
    result.frames = 4096;
    result.overall_confidence = 0.93;
    result.artifacts.push_back({.kind = amt::separation::CacheArtifactKind::time_frequency_mask,
                                .role = amt::separation::StemRole::bass,
                                .path = mask_path,
                                .confidence = 0.91});
    amt::core::report_progress(progress, 1.0);
    return result;
  }

  int calls{0};
};

amt::separation::SourceGuidanceRequest guided_request(const std::filesystem::path& source) {
  amt::separation::SourceGuidanceRequest request;
  request.separation.source_path = source;
  request.separation.requested_stems = {amt::separation::StemRole::bass};
  request.separation.request_stem_audio = false;
  request.separation.request_time_frequency_masks = true;
  request.evidence = {.source_specific_issue = true,
                      .reconstruction_required_for_full_repair = false,
                      .expected_repair_benefit = 0.65,
                      .source_guidance_confidence = 0.88,
                      .source_guided_stereo_sufficiency = 0.80,
                      .model_confidence = 0.0};
  return request;
}

void test_same_path_replacement_gets_new_cache_identity() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase5-source-identity-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  const auto source = root / "source.wav";
  write_file(source, "source-version-one");

  CountingMaskProvider provider;
  amt::separation::SeparationCache cache(root / "cache");
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider, nullptr, &cache);
  const auto request = guided_request(source);
  std::string error;

  const auto first = orchestrator.execute(request, error);
  assert(first.has_value());
  assert(error.empty());
  assert(first->decision.mode == amt::separation::SeparationMode::source_guided_stereo);
  assert(first->provider_invoked);
  assert(!first->cache_hit);
  assert(provider.calls == 1);

  const auto second = orchestrator.execute(request, error);
  assert(second.has_value());
  assert(error.empty());
  assert(second->cache_hit);
  assert(!second->provider_invoked);
  assert(provider.calls == 1);

  write_file(source, "source-version-two-with-different-content");
  const auto replaced = orchestrator.execute(request, error);
  assert(replaced.has_value());
  assert(error.empty());
  assert(!replaced->cache_hit);
  assert(replaced->provider_invoked);
  assert(provider.calls == 2);

  std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main() {
  test_standard_sha256_vectors();
  test_same_path_replacement_gets_new_cache_identity();
  return 0;
}



