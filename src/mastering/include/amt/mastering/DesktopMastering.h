#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "amt/mastering/OfflineRenderer.h"

namespace amt::mastering {

struct DesktopMasteringReport {
  bool source_diagnostics_performed{false};
  bool source_guidance_applied{false};
  bool automatic_mode1_approved{false};
  std::string summary;
  std::string json;
};

// Desktop-facing Phase 5 mastering entry point. It preserves the established
// Phase 4 call shape while allowing the adapter to replace the caller's plan with
// the effective plan that actually rendered Master A/B. When supplied, report
// receives persistent source-diagnostic provenance for project history/UI restore.
[[nodiscard]] std::optional<MasteringRenderPair> render_mastering_plan_for_desktop(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    MasteringPlan& plan,
    std::string& error,
    const RenderSettings& settings = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {},
    DesktopMasteringReport* report = nullptr);

}  // namespace amt::mastering
