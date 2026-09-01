#include "amt/mastering/DesktopMastering.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace amt::mastering {
namespace {

bool write_phase5_text_atomic(const std::filesystem::path& path,
                              const std::string& content) {
  std::error_code directory_error;
  std::filesystem::create_directories(path.parent_path(), directory_error);
  if (directory_error) return false;

  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return false;
    }
  }

  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
  if (remove_error) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }

  std::error_code rename_error;
  std::filesystem::rename(temporary, path, rename_error);
  if (rename_error) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  return true;
}

bool persist_phase5_desktop_report(
    const std::filesystem::path& output_directory,
    const DesktopMasteringReport& report) {
  const auto project_directory = output_directory.parent_path();
  if (project_directory.empty()) return false;

  const bool json_ok = write_phase5_text_atomic(
      project_directory / "source-diagnostics-v1.json", report.json);
  const bool summary_ok = write_phase5_text_atomic(
      project_directory / "source-diagnostics-v1.txt", report.summary + "\n");
  return json_ok && summary_ok;
}

}  // namespace

// Phase 4's Windows shell calls the original renderer shape. Keep that shell
// untouched while routing the call through Phase 5 and persisting the diagnostic
// provenance beside the project's render directory for later inspection/restore.
[[nodiscard]] std::optional<MasteringRenderPair>
render_mastering_plan_phase5_desktop(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    MasteringPlan& plan,
    std::string& error,
    const RenderSettings& settings = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {}) {
  DesktopMasteringReport report;
  auto rendered = render_mastering_plan_for_desktop(
      codecs, canonical_input, output_directory, source_analysis, plan, error,
      settings, cancellation, progress, &report);
  if (!rendered) return std::nullopt;

  if (!persist_phase5_desktop_report(output_directory, report)) {
    const std::string warning =
        "Source diagnostics were available for this session, but the persistent diagnostic sidecar could not be written.";
    plan.master_a.rationale.push_back(warning);
    plan.master_b.rationale.push_back(warning);
  }
  return rendered;
}

}  // namespace amt::mastering

// Keep the established Windows UI/workflow shell while routing its mastering call
// through the Phase 5 adapter and persistence wrapper.
#define render_mastering_plan render_mastering_plan_phase5_desktop
#include "Phase4Main.cpp"
#undef render_mastering_plan
