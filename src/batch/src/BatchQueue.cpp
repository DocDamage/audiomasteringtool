#include "amt/batch/BatchQueue.h"
#include <iomanip>
#include <sstream>
#include <system_error>
#include "amt/analysis/FileAnalyzer.h"
#include "amt/batch/CohesionPlanner.h"
#include "amt/mastering/OfflineRenderer.h"
#include "amt/mastering/Planner.h"

namespace amt::batch {

BatchQueue::BatchQueue(std::shared_ptr<amt::codec::ICodecService> codecs)
    : codecs_(std::move(codecs)) {}

bool BatchQueue::process_album(
    BatchAlbumProject& album,
    const std::filesystem::path& output_directory,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const BatchProgressCallback& progress) {
  error.clear();
  if (album.empty()) {
    error = "Album project has no tracks";
    return false;
  }

  if (!codecs_ || !codecs_->available()) {
    error = "Codec service unavailable";
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(output_directory, ec);

  // Phase 1: Analyze all tracks that need analysis
  for (std::size_t i = 0; i < album.tracks.size(); ++i) {
    if (cancellation && cancellation->is_cancelled()) {
      error = "Batch processing cancelled";
      return false;
    }

    auto& track = album.tracks[i];
    track.status = TrackStatus::analyzing;

    std::string track_err;
    auto report = amt::analysis::analyze_file(*codecs_, track.source_path, track_err, cancellation);
    if (report) {
      track.input_lufs = report->loudness.integrated_lufs;
      track.input_true_peak = report->loudness.true_peak_dbtp;
      track.status = TrackStatus::pending;
    } else {
      track.status = TrackStatus::error;
      track.error_message = track_err;
    }

    if (progress) {
      progress(i, album.tracks.size(), 0.5);
    }
  }

  // Phase 2: Compute cohesion plan if enabled
  std::vector<TrackCohesionPlan> cohesion_plans;
  if (album.cohesion_enabled) {
    cohesion_plans = CohesionPlanner::plan_cohesion(
        album.tracks, album.target_album_lufs, album.target_ceiling);
  }

  // Phase 3: Master each track
  for (std::size_t i = 0; i < album.tracks.size(); ++i) {
    if (cancellation && cancellation->is_cancelled()) {
      error = "Batch processing cancelled";
      return false;
    }

    auto& track = album.tracks[i];
    if (track.status == TrackStatus::error) {
      continue; // isolate failure, keep processing remainder
    }

    track.status = TrackStatus::mastering;

    // Apply cohesion targets
    if (i < cohesion_plans.size()) {
      track.target_lufs = cohesion_plans[i].target_lufs;
      track.target_ceiling = cohesion_plans[i].target_ceiling_dbtp;
    } else {
      track.target_lufs = album.target_album_lufs;
      track.target_ceiling = album.target_ceiling;
    }

    // Format output filename: "01 - TrackName.wav"
    std::ostringstream name_ss;
    name_ss << std::setw(2) << std::setfill('0') << (i + 1) << " - "
            << (track.title.empty() ? track.source_path.stem().string() : track.title)
            << ".wav";
    const auto track_out = output_directory / name_ss.str();

    std::string plan_err;
    auto report = amt::analysis::analyze_file(*codecs_, track.source_path, plan_err, cancellation);
    if (!report) {
      track.status = TrackStatus::error;
      track.error_message = "Analysis failed: " + plan_err;
      continue;
    }

    auto plan = amt::mastering::plan_mastering(*report);
    // Override target LUFS with cohesion target
    plan.master_a.target_lufs = track.target_lufs;
    plan.master_a.ceiling_dbtp = track.target_ceiling;

    std::string render_err;
    auto rendered = amt::mastering::render_candidate(
        *codecs_, track.source_path, track_out, plan.master_a, render_err, {}, cancellation);

    if (rendered) {
      track.status = TrackStatus::ready;
      track.master_output_path = track_out;
      track.master_lufs = rendered->analysis.loudness.integrated_lufs;
      track.master_true_peak = rendered->analysis.loudness.true_peak_dbtp;
    } else {
      track.status = TrackStatus::error;
      track.error_message = render_err;
    }

    if (progress) {
      progress(i + 1, album.tracks.size(), 1.0);
    }
  }

  return true;
}

}  // namespace amt::batch
