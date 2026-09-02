#pragma once

#include <filesystem>
#include <string>
#include "amt/batch/BatchProject.h"

namespace amt::batch {

class BatchExport {
 public:
  [[nodiscard]] static std::string generate_album_report(
      const BatchAlbumProject& album);

  static bool write_album_manifest(
      const BatchAlbumProject& album,
      const std::filesystem::path& manifest_path,
      std::string& error);
};

}  // namespace amt::batch
