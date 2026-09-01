#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "amt/separation/Separation.h"

namespace amt::separation {

struct WorkerSeparationModelContract {
  std::string input_tensor_name{"audio"};
  std::string output_tensor_name{"stems"};
  std::size_t chunk_frames{262144U};
  std::size_t overlap_frames{16384U};
  double calibrated_output_confidence{0.0};
  bool complete_reconstruction{false};
};

struct WorkerSeparationProviderConfig {
  std::filesystem::path worker_executable;
  std::filesystem::path model_artifact;
  std::filesystem::path fallback_output_root;
  SeparationModelManifest manifest;
  WorkerSeparationModelContract contract;
  std::string execution_provider{"cpu"};
  std::size_t maximum_worker_output_bytes{64U * 1024U};
  std::uint64_t maximum_runtime_seconds{1800U};
};

// Windows-first production provider. Heavy model execution remains in amt_worker;
// the mastering/UI process only validates model identity, launches the isolated
// job, validates output geometry, and exposes managed stem references.
class WorkerSeparationProvider final : public ISeparationProvider {
 public:
  explicit WorkerSeparationProvider(WorkerSeparationProviderConfig config);

  [[nodiscard]] bool available() const noexcept override;
  [[nodiscard]] SeparationModelManifest model_manifest() const override;
  [[nodiscard]] std::optional<SeparationResult> separate(
      const SeparationRequest& request,
      std::string& error,
      const amt::core::CancellationToken* cancellation = nullptr,
      const amt::core::ProgressCallback& progress = {}) override;

  [[nodiscard]] const WorkerSeparationProviderConfig& config() const noexcept {
    return config_;
  }

 private:
  WorkerSeparationProviderConfig config_;
};

}  // namespace amt::separation
