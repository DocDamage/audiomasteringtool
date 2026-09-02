#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "amt/batch/BatchProject.h"
#include "amt/codec/AudioIO.h"
#include "amt/core/JobControl.h"

namespace amt::batch {

using BatchProgressCallback = std::function<void(std::size_t completed_tracks, std::size_t total_tracks, double current_track_progress)>;

class BatchQueue {
 public:
  explicit BatchQueue(std::shared_ptr<amt::codec::ICodecService> codecs);

  bool process_album(
      BatchAlbumProject& album,
      const std::filesystem::path& output_directory,
      std::string& error,
      const amt::core::CancellationToken* cancellation = nullptr,
      const BatchProgressCallback& progress = {});

 private:
  std::shared_ptr<amt::codec::ICodecService> codecs_;
};

}  // namespace amt::batch
