#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "amt/analysis/FileAnalyzer.h"
#include "amt/app/DesktopViewModel.h"
#include "amt/codec/AudioIO.h"
#include "amt/core/JobControl.h"
#include "amt/mastering/DesktopMastering.h"
#include "amt/mastering/OfflineRenderer.h"
#include "amt/mastering/Planner.h"
#include "amt/project/ExportRecipes.h"
#include "amt/project/ProjectStore.h"

namespace amt::app {

class MasteringSessionController {
 public:
  explicit MasteringSessionController(
      std::shared_ptr<amt::codec::ICodecService> codecs = nullptr,
      std::shared_ptr<amt::project::ProjectStore> store = nullptr);

  ~MasteringSessionController() = default;

  [[nodiscard]] bool open_source(
      const std::filesystem::path& source_path,
      std::string& error,
      const amt::core::CancellationToken* cancellation = nullptr,
      const amt::core::ProgressCallback& progress = {});

  [[nodiscard]] bool run_mastering(
      std::string& error,
      const amt::core::CancellationToken* cancellation = nullptr,
      const amt::core::ProgressCallback& progress = {});

  [[nodiscard]] bool select_candidate(
      AuditionTarget target,
      std::string& error);

  [[nodiscard]] bool export_selected(
      const std::string& recipe_id,
      const std::filesystem::path& destination_path,
      std::string& error,
      const amt::core::CancellationToken* cancellation = nullptr,
      const amt::core::ProgressCallback& progress = {});

  [[nodiscard]] const TrackViewModel& view_model() const noexcept { return model_; }
  [[nodiscard]] AppState state() const noexcept { return state_; }

  void restore_sidecar_diagnostics(const std::filesystem::path& sidecar_json_path);

 private:
  std::shared_ptr<amt::codec::ICodecService> codecs_;
  std::shared_ptr<amt::project::ProjectStore> store_;
  TrackViewModel model_;
  AppState state_{AppState::idle};
  std::optional<amt::analysis::Phase1AnalysisReport> analysis_;
  std::optional<amt::mastering::MasteringPlan> plan_;
  std::optional<amt::mastering::MasteringRenderPair> masters_;
  std::optional<amt::project::ProjectRecord> project_;
};

}  // namespace amt::app
