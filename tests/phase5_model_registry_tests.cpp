#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "amt/separation/ModelRegistry.h"
#include "amt/separation/Separation.h"

namespace {

std::filesystem::path test_root(const std::string& name) {
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("amt-phase5-model-registry-" + name + "-" + std::to_string(ticks));
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string valid_active_registry(const std::string& overrides = {}) {
  std::string registry = R"json({
  "schemaVersion": 2,
  "activeSeparationModel": "synthetic-separator",
  "models": [
    {
      "id": "synthetic-separator",
      "version": "1.0.0",
      "task": "source-separation",
      "source": "owned synthetic architecture",
      "weightProvenance": "owned synthetic weights",
      "artifact": "synthetic/model.onnx",
      "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "runtime": "onnxruntime-worker-v1",
      "codeLicense": "MIT",
      "weightsLicense": "Internal commercial test license",
      "commercialUseReviewed": true,
      "commercialUse": true,
      "redistributionReviewed": true,
      "redistributionAllowed": true,
      "attribution": "Synthetic fixture",
      "ramMb": 1024,
      "vramMb": 2048,
      "securityReview": "approved",
      "benchmarkRecord": "synthetic-benchmark-v1",
      "executionProviders": ["cpu", "cuda"],
      "inputSampleRate": 48000,
      "stemTaxonomy": ["vocals", "drums", "bass", "other"],
      "automaticMode1Approved": false,
      "onnxContract": {
        "inputTensor": "audio",
        "outputTensor": "stems",
        "chunkFrames": 262144,
        "overlapFrames": 16384,
        "calibratedOutputConfidence": 0.91,
        "completeReconstruction": false
      }
    }
  ]
})json";

  if (!overrides.empty()) {
    const auto separator = overrides.find('=');
    assert(separator != std::string::npos);
    const std::string needle = overrides.substr(0U, separator);
    const std::string replacement = overrides.substr(separator + 1U);
    const auto position = registry.find(needle);
    assert(position != std::string::npos);
    registry.replace(position, needle.size(), replacement);
  }
  return registry;
}

bool warning_contains(const std::vector<std::string>& warnings,
                      const std::string& needle) {
  return std::any_of(warnings.begin(), warnings.end(), [&](const std::string& warning) {
    return warning.find(needle) != std::string::npos;
  });
}

void test_empty_registry_is_valid_stereo_only_state() {
  const auto root = test_root("empty");
  const auto registry = root / "models" / "registry.json";
  write_text(registry, R"json({
    "schemaVersion": 2,
    "activeSeparationModel": null,
    "models": []
  })json");

  std::string error;
  const auto selection = amt::separation::load_model_registry_selection(
      registry, root / "amt_worker.exe", error);
  assert(selection.has_value());
  assert(error.empty());
  assert(!selection->active_separation_model.has_value());

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void test_valid_active_registry_maps_worker_contract() {
  const auto root = test_root("valid");
  const auto registry = root / "models" / "registry.json";
  write_text(registry, valid_active_registry());

  std::string error;
  const auto selection = amt::separation::load_model_registry_selection(
      registry, root / "amt_worker.exe", error, root / "source-estimates");
  assert(selection.has_value());
  assert(error.empty());
  assert(selection->active_separation_model.has_value());

  const auto& config = *selection->active_separation_model;
  assert(config.manifest.model_name == "synthetic-separator");
  assert(config.manifest.model_version == "1.0.0");
  assert(config.manifest.expected_input_sample_rate == 48000);
  assert(config.manifest.stem_taxonomy.size() == 4U);
  assert(config.manifest.stem_taxonomy[0] == amt::separation::StemRole::vocals);
  assert(config.manifest.stem_taxonomy[1] == amt::separation::StemRole::drums);
  assert(config.manifest.stem_taxonomy[2] == amt::separation::StemRole::bass);
  assert(config.manifest.stem_taxonomy[3] == amt::separation::StemRole::other);
  assert(config.contract.input_tensor_name == "audio");
  assert(config.contract.output_tensor_name == "stems");
  assert(config.contract.chunk_frames == 262144U);
  assert(config.contract.overlap_frames == 16384U);
  assert(config.contract.calibrated_output_confidence == 0.91);
  assert(!config.contract.complete_reconstruction);
  assert(!config.automatic_mode1_approved);
  assert(config.execution_provider == "cpu");
  assert(config.model_artifact == root / "models" / "synthetic" / "model.onnx");
  assert(config.fallback_output_root == root / "source-estimates");
  assert(!warning_contains(selection->warnings, "artifact is not present"));

  const auto eligibility =
      amt::separation::evaluate_model_for_bundled_production(config.manifest);
  assert(eligibility.eligible_for_bundled_production);

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void test_unknown_active_model_is_rejected() {
  const auto root = test_root("unknown-active");
  const auto registry = root / "models" / "registry.json";
  write_text(registry, valid_active_registry(
      "\"activeSeparationModel\": \"synthetic-separator\"="
      "\"activeSeparationModel\": \"missing\""));

  std::string error;
  const auto selection = amt::separation::load_model_registry_selection(
      registry, root / "amt_worker.exe", error);
  assert(!selection.has_value());
  assert(error.find("does not match") != std::string::npos);

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void test_artifact_escape_is_rejected() {
  const auto root = test_root("artifact-escape");
  const auto registry = root / "models" / "registry.json";
  write_text(registry, valid_active_registry(
      "\"artifact\": \"synthetic/model.onnx\"="
      "\"artifact\": \"../outside.onnx\""));

  std::string error;
  const auto selection = amt::separation::load_model_registry_selection(
      registry, root / "amt_worker.exe", error);
  assert(!selection.has_value());
  assert(error.find("models directory") != std::string::npos);

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void test_active_model_requires_cpu_fallback() {
  const auto root = test_root("cpu-fallback");
  const auto registry = root / "models" / "registry.json";
  write_text(registry, valid_active_registry(
      "\"executionProviders\": [\"cpu\", \"cuda\"]="
      "\"executionProviders\": [\"cuda\"]"));

  std::string error;
  const auto selection = amt::separation::load_model_registry_selection(
      registry, root / "amt_worker.exe", error);
  assert(!selection.has_value());
  assert(error.find("CPU") != std::string::npos);

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void test_bad_onnx_contract_is_rejected() {
  const auto root = test_root("contract");
  const auto registry = root / "models" / "registry.json";
  write_text(registry, valid_active_registry(
      "\"calibratedOutputConfidence\": 0.91="
      "\"calibratedOutputConfidence\": 0.0"));

  std::string error;
  const auto selection = amt::separation::load_model_registry_selection(
      registry, root / "amt_worker.exe", error);
  assert(!selection.has_value());
  assert(error.find("bounds/confidence") != std::string::npos);

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void test_mode1_approval_is_data_driven() {
  const auto root = test_root("mode1-approval");
  const auto registry = root / "models" / "registry.json";
  write_text(registry, valid_active_registry(
      "\"automaticMode1Approved\": false="
      "\"automaticMode1Approved\": true"));

  std::string error;
  const auto selection = amt::separation::load_model_registry_selection(
      registry, root / "amt_worker.exe", error);
  assert(selection.has_value());
  assert(error.empty());
  assert(selection->active_separation_model.has_value());
  assert(selection->active_separation_model->automatic_mode1_approved);

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void test_missing_mode1_approval_is_rejected() {
  const auto root = test_root("mode1-missing");
  const auto registry = root / "models" / "registry.json";
  write_text(registry, valid_active_registry(
      "      \"automaticMode1Approved\": false,\n="));

  std::string error;
  const auto selection = amt::separation::load_model_registry_selection(
      registry, root / "amt_worker.exe", error);
  assert(!selection.has_value());
  assert(error.find("automaticMode1Approved") != std::string::npos);

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void test_unapproved_security_review_is_not_production_eligible() {
  const auto root = test_root("security");
  const auto registry = root / "models" / "registry.json";
  write_text(registry, valid_active_registry(
      "\"securityReview\": \"approved\"="
      "\"securityReview\": \"pending\""));

  std::string error;
  const auto selection = amt::separation::load_model_registry_selection(
      registry, root / "amt_worker.exe", error);
  assert(selection.has_value());
  assert(error.empty());
  assert(selection->active_separation_model.has_value());
  assert(!selection->active_separation_model->manifest.security_reviewed);
  assert(warning_contains(selection->warnings, "not eligible"));
  assert(warning_contains(selection->warnings, "security review"));

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main() {
  test_empty_registry_is_valid_stereo_only_state();
  test_valid_active_registry_maps_worker_contract();
  test_unknown_active_model_is_rejected();
  test_artifact_escape_is_rejected();
  test_active_model_requires_cpu_fallback();
  test_bad_onnx_contract_is_rejected();
  test_mode1_approval_is_data_driven();
  test_missing_mode1_approval_is_rejected();
  test_unapproved_security_review_is_not_production_eligible();
  return 0;
}
