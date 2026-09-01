#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "amt/mastering/OfflineRenderer.h"

namespace amt::mastering {

// Desktop-facing Phase 5 mastering entry point. It preserves the established
// Phase 4 call shape while allowing the adapter to replace the caller's plan with
// the effective plan that actually rendered Master A/B.
[[nodiscard]] std::optional<MasteringRenderPair> render_mastering_plan_for_desktop(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    MasteringPlan& plan,
    std::string& error,
    const RenderSettings& settings = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::mastering
