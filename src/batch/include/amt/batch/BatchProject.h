#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace amt::batch {

enum class TrackStatus {
  pending,
  analyzing,
  mastering,
  ready,
  error
};

struct BatchTrackItem {
  std::size_t track_index{0};
  std::filesystem::path source_path;
  std::string title;
  std::string artist;
  double input_lufs{-80.0};
  double input_true_peak{-80.0};
  double target_lufs{-14.0};
  double target_ceiling{-1.0};
  TrackStatus status{TrackStatus::pending};
  std::string error_message;
  std::filesystem::path master_output_path;
  double master_lufs{-80.0};
  double master_true_peak{-80.0};
};

struct BatchAlbumProject {
  std::string album_id;
  std::string album_name{"Untitled Album"};
  std::string artist{"Unknown Artist"};
  double target_album_lufs{-14.0};
  double target_ceiling{-1.0};
  bool cohesion_enabled{true};
  std::vector<BatchTrackItem> tracks;
  std::int64_t created_ms{0};
  std::int64_t updated_ms{0};

  [[nodiscard]] std::size_t count() const noexcept { return tracks.size(); }
  [[nodiscard]] bool empty() const noexcept { return tracks.empty(); }
};

}  // namespace amt::batch
