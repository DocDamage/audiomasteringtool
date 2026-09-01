#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "amt/core/JobControl.h"

namespace amt::core {

struct FileFingerprint {
  std::uintmax_t size_bytes{0U};
  std::string sha256;
};

[[nodiscard]] std::optional<FileFingerprint> fingerprint_file_sha256(
    const std::filesystem::path& path,
    std::string& error,
    const CancellationToken* cancellation = nullptr,
    const ProgressCallback& progress = {});

}  // namespace amt::core
