#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/separation/WorkerSeparationProvider.h"

namespace amt::separation {

struct ModelRegistrySelection {
  std::optional<WorkerSeparationProviderConfig> active_separation_model;
  std::vector<std::string> warnings;
};

// Loads the deliberately activated separation model from models/registry.json.
// No active model is a valid state and results in stereo-only operation. Invalid
// registry structure is an error; unavailable artifacts are reported as warnings
// and left for WorkerSeparationProvider::available() to reject safely.
[[nodiscard]] std::optional<ModelRegistrySelection> load_model_registry_selection(
    const std::filesystem::path& registry_path,
    const std::filesystem::path& worker_executable,
    std::string& error,
    const std::filesystem::path& fallback_output_root = {});

}  // namespace amt::separation
