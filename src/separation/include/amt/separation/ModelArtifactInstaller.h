#pragma once

#include <optional>
#include <string>

#include "amt/separation/WorkerSeparationProvider.h"

namespace amt::separation {

struct ModelArtifactInstallResult {
  bool already_present{false};
  bool downloaded{false};
};

// Installs only model artifacts that match AudioMasteringTool's compiled trusted
// catalog (model id/version/SHA). Registry content cannot supply an arbitrary URL.
// The download is written to a sidecar, size/SHA verified, then atomically published.
[[nodiscard]] std::optional<ModelArtifactInstallResult> ensure_model_artifact_installed(
    const WorkerSeparationProviderConfig& config,
    std::string& error,
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::separation
