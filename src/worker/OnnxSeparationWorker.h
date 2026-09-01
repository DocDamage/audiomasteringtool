#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/core/JobControl.h"

namespace amt::worker {

struct OnnxSeparationWorkerRequest {
  std::filesystem::path model_path;
  std::filesystem::path source_path;
  std::filesystem::path output_directory;
  int input_sample_rate{0};
  std::vector<std::string> stem_names;
  std::string input_tensor_name{"audio"};
  std::string output_tensor_name{"stems"};
  std::size_t chunk_frames{262144U};
  std::size_t overlap_frames{16384U};
  std::string execution_provider{"cpu"};
};

struct OnnxSeparationWorkerResult {
  int sample_rate{0};
  std::int64_t frames{0};
  std::vector<std::filesystem::path> stem_paths;
};

[[nodiscard]] bool onnx_separation_compiled() noexcept;

// Contract:
// input  `audio`: float32 [1, 2, frames]
// output `stems`: float32 [1, stem_count, 2, frames]
// The worker runs bounded overlapping chunks and streams float WAV stem outputs.
[[nodiscard]] std::optional<OnnxSeparationWorkerResult> run_onnx_separation(
    const OnnxSeparationWorkerRequest& request,
    std::string& error,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::worker
