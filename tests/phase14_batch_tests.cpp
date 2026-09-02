#include <cassert>
#include <filesystem>
#include <iostream>
#include "amt/batch/BatchExport.h"
#include "amt/batch/BatchProject.h"
#include "amt/batch/BatchQueue.h"
#include "amt/batch/CohesionPlanner.h"
#include "amt/batch/CollectionAnalysis.h"
#include "amt/batch/SequenceAudition.h"

int main() {
  std::cout << "[Phase 14] Running Album / Batch Mastering Tests...\n";

  // Test 1: Collection analysis metrics
  {
    amt::batch::BatchAlbumProject album;
    album.album_name = "Mastering Showcase EP";
    album.artist = "Demo Artist";
    album.target_album_lufs = -14.0;

    album.tracks.push_back({.track_index = 0, .title = "Intro Track", .input_lufs = -18.0});
    album.tracks.push_back({.track_index = 1, .title = "Single Banger", .input_lufs = -11.0});
    album.tracks.push_back({.track_index = 2, .title = "Acoustic Interlude", .input_lufs = -20.0});
    album.tracks.push_back({.track_index = 3, .title = "Outro Anthem", .input_lufs = -13.0});

    auto stats = amt::batch::CollectionAnalysis::analyze_collection(album.tracks);
    assert(stats.track_count == 4);
    assert(stats.min_lufs == -20.0);
    assert(stats.max_lufs == -11.0);
    assert(stats.dynamic_span_lu == 9.0);
    assert(stats.mean_lufs < -14.0);
    std::cout << "  ✓ Test 1: Computed accurate collection-wide loudness statistics\n";
  }

  // Test 2: Cohesion planning balances album flow while preserving intentional dynamic contrast
  {
    amt::batch::BatchAlbumProject album;
    album.target_album_lufs = -14.0;
    album.tracks.push_back({.track_index = 0, .title = "Quiet Ballad", .input_lufs = -22.0});
    album.tracks.push_back({.track_index = 1, .title = "Peak Single", .input_lufs = -10.0});
    album.tracks.push_back({.track_index = 2, .title = "Mid Energy", .input_lufs = -15.0});

    auto plans = amt::batch::CohesionPlanner::plan_cohesion(album.tracks, -14.0, -1.0);
    assert(plans.size() == 3);
    // Quiet ballad should be pulled up towards -14 but stay slightly softer than the peak single
    assert(plans[0].target_lufs < plans[1].target_lufs);
    assert(!plans[0].explanation.empty());
    assert(!plans[1].explanation.empty());
    std::cout << "  ✓ Test 2: CohesionPlanner balanced album dynamics preserving contrast\n";
  }

  // Test 3: Sequence audition transition analysis
  {
    amt::batch::BatchAlbumProject album;
    album.tracks.push_back({.track_index = 0, .master_lufs = -14.0});
    album.tracks.push_back({.track_index = 1, .master_lufs = -13.5});
    album.tracks.push_back({.track_index = 2, .master_lufs = -18.0}); // large drop

    auto transitions = amt::batch::SequenceAudition::analyze_transitions(album);
    assert(transitions.size() == 2);
    assert(!transitions[0].has_loudness_jump_warning); // 0.5 LU jump is normal
    assert(transitions[1].has_loudness_jump_warning);  // 4.5 LU jump triggers warning
    std::cout << "  ✓ Test 3: SequenceAudition flagged track-to-track transition jumps\n";
  }

  // Test 4: Album report and manifest generation
  {
    amt::batch::BatchAlbumProject album;
    album.album_name = "Greatest Masters";
    album.artist = "AMT Master";
    album.target_album_lufs = -14.0;
    album.tracks.push_back({.track_index = 0, .title = "Track One", .input_lufs = -16.0,
                            .status = amt::batch::TrackStatus::ready, .master_output_path = "01 - Track One.wav",
                            .master_lufs = -14.1, .master_true_peak = -1.0});

    auto report = amt::batch::BatchExport::generate_album_report(album);
    assert(!report.empty());
    assert(report.find("Album Mastering Report") != std::string::npos);

    const auto temp_manifest = std::filesystem::temp_directory_path() / "album_manifest.json";
    std::string err;
    bool ok = amt::batch::BatchExport::write_album_manifest(album, temp_manifest, err);
    assert(ok);
    assert(std::filesystem::exists(temp_manifest));
    std::error_code ec;
    std::filesystem::remove(temp_manifest, ec);
    std::cout << "  ✓ Test 4: BatchExport created album report and JSON manifest\n";
  }

  std::cout << "[Phase 14] All tests passed successfully!\n";
  return 0;
}
