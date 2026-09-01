#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/codec/AudioIO.h"
#include "amt/core/JobControl.h"
#include "amt/mastering/OfflineRenderer.h"
#include "amt/separation/Separation.h"
#include "amt/separation/SourceGuidedProcessing.h"

namespace amt::mastering {

struct SourceGuidedCalibrationRequest {
  std::filesystem::path source_path;
  std::filesystem::path output_directory;
  std::filesystem::path registry_path;
  std::filesystem::path worker_executable;
  std::filesystem::path model_store_root;
  RenderSettings render_settings;
};

struct SourceGuidedCalibrationResult {
  bool source_estimates_analyzed{false};
  bool guided_candidate_rendered{false};
  amt::separation::SeparationMode evidence_mode{
      amt::separation::SeparationMode::stereo_mastering};
  std::filesystem::path stereo_master_a;
  std::filesystem::path guided_master_a;
  std::filesystem::path manifest_path;
  std::string model_name;
  std::string model_version;
  std::vector<amt::separation::SourceGuidedIssue> issues;
  std::vector<std::string> warnings;
};

// Generates a normal stereo Master A plus, when the measured source evidence
// independently qualifies for Mode 1, a source-guided Master A. This deliberately
// bypasses only the registry's automaticMode1Approved product gate so the exact
// candidate that would be considered for automatic use can be evaluated blindly.
// It does not weaken the source-evidence policy, canonical-stereo constraint, or
// bounded source-guided processing limits.
[[nodiscard]] std::optional<SourceGuidedCalibrationResult>
render_source_guided_calibration_pair(
    amt::codec::ICodecService& codecs,
    const SourceGuidedCalibrationRequest& request,
    std::string& error,
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::mastering
