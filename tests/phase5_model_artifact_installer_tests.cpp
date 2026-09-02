#include <cassert>
#include <filesystem>
#include <string>

#include "amt/core/JobControl.h"
#include "amt/separation/ModelArtifactInstaller.h"
#include "amt/separation/Separation.h"
#include "amt/separation/WorkerSeparationProvider.h"

namespace {

amt::separation::WorkerSeparationProviderConfig trusted_htdemucs_config() {
  amt::separation::WorkerSeparationProviderConfig config;
  config.model_artifact =
      std::filesystem::temp_directory_path() /
      "amt-phase5-installer-test" /
      "htdemucs_fp16weights.onnx";
  config.manifest.model_name = "htdemucs-onnx-fp16weights";
  config.manifest.model_version =
      "d54ed9eb60e258ea82131c6ee14578628816456a";
  config.manifest.model_sha256 =
      "d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a";
  config.manifest.architecture_source = "trusted installer fixture";
  config.manifest.weight_provenance = "trusted installer fixture";
  config.manifest.code_license = "MIT";
  config.manifest.weights_license = "MIT";
  config.manifest.redistribution_reviewed = true;
  config.manifest.redistribution_allowed = true;
  config.manifest.commercial_use_reviewed = true;
  config.manifest.commercial_use_allowed = true;
  config.manifest.supported_execution_providers = {"cpu"};
  config.manifest.expected_input_sample_rate = 44100;
  config.manifest.stem_taxonomy = {
      amt::separation::StemRole::drums,
      amt::separation::StemRole::bass,
      amt::separation::StemRole::other,
      amt::separation::StemRole::vocals};
  config.manifest.benchmark_record = "trusted installer fixture";
  config.manifest.security_reviewed = true;
  config.contract.input_tensor_name = "mix";
  config.contract.output_tensor_name = "stems";
  config.contract.chunk_frames = 343980U;
  config.contract.overlap_frames = 85995U;
  config.contract.calibrated_output_confidence = 0.70;
  return config;
}

void test_unknown_model_never_reaches_network() {
  auto config = trusted_htdemucs_config();
  config.manifest.model_name = "not-in-trusted-catalog";

  std::string error;
  const auto result = amt::separation::ensure_model_artifact_installed(
      config, error);
  assert(!result.has_value());
  assert(error.find("trusted download catalog") != std::string::npos);
}

void test_pre_cancelled_trusted_install_never_reaches_network() {
  auto config = trusted_htdemucs_config();
  amt::core::CancellationToken cancellation;
  cancellation.cancel();

  std::string error;
  const auto result = amt::separation::ensure_model_artifact_installed(
      config, error, &cancellation);
  assert(!result.has_value());
  assert(cancellation.is_cancelled());
  assert(error.find("cancelled") != std::string::npos);
}

}  // namespace

int main() {
  test_unknown_model_never_reaches_network();
  test_pre_cancelled_trusted_install_never_reaches_network();
  return 0;
}
