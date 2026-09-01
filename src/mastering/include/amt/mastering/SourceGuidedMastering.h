#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/mastering/OfflineRenderer.h"
#include "amt/separation/SourceControlEnvelope.h"
#include "amt/separation/SourceGuidance.h"
#include "amt/separation/SourceGuidedProcessing.h"
#include "amt/separation/SourceGuidedStereoExecutor.h"

namespace amt::mastering {

struct SourceGuidedMasteringConfig {
  amt::separation::SourceGuidedProcessingConfig processing;
  amt::separation::SourceControlEnvelopeConfig envelopes;
  amt::separation::SourceGuidedStereoExecutorConfig executor;
  bool fallback_to_stereo_mastering{true};
};

struct SourceGuidedMasteringRenderPair {
  MasteringRenderPair masters;
  bool source_guidance_applied{false};
  amt::separation::SeparationMode requested_mode{
      amt::separation::SeparationMode::stereo_mastering};
  amt::separation::SeparationMode rendered_mode{
      amt::separation::SeparationMode::stereo_mastering};
  std::size_t applied_bindings{0U};
  std::vector<std::string> warnings;
};

[[nodiscard]] std::optional<SourceGuidedMasteringRenderPair>
render_mastering_plan_with_source_guidance(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    const MasteringPlan& plan,
    const amt::separation::SourceGuidanceResult& guidance,
    const std::vector<amt::separation::SourceGuidedIssue>& issues,
    std::string& error,
    const SourceGuidedMasteringConfig& config = {},
    const RenderSettings& render_settings = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::mastering
