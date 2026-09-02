#include "amt/batch/BatchExport.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace amt::batch {

std::string BatchExport::generate_album_report(const BatchAlbumProject& album) {
  std::ostringstream ss;
  ss << "# Album Mastering Report: " << album.album_name << "\n\n"
     << "**Artist:** " << album.artist << "\n"
     << "**Target Loudness:** " << album.target_album_lufs << " LUFS\n"
     << "**Target Ceiling:** " << album.target_ceiling << " dBTP\n"
     << "**Tracks:** " << album.tracks.size() << "\n\n"
     << "## Track Sequence Summary\n\n"
     << "| # | Title | Input LUFS | Master LUFS | Peak (dBTP) | Status |\n"
     << "|---|---|---|---|---|---|\n";

  for (std::size_t i = 0; i < album.tracks.size(); ++i) {
    const auto& t = album.tracks[i];
    ss << "| " << (i + 1) << " | "
       << (t.title.empty() ? t.source_path.filename().string() : t.title) << " | "
       << std::fixed << std::setprecision(1) << t.input_lufs << " | "
       << t.master_lufs << " | "
       << t.master_true_peak << " | "
       << (t.status == TrackStatus::ready ? "Ready" : "Error") << " |\n";
  }

  return ss.str();
}

bool BatchExport::write_album_manifest(
    const BatchAlbumProject& album,
    const std::filesystem::path& manifest_path,
    std::string& error) {
  error.clear();
  std::ofstream out(manifest_path);
  if (!out.is_open()) {
    error = "Could not create album manifest: " + manifest_path.string();
    return false;
  }

  out << "{\n"
      << "  \"album_name\": \"" << album.album_name << "\",\n"
      << "  \"artist\": \"" << album.artist << "\",\n"
      << "  \"target_album_lufs\": " << album.target_album_lufs << ",\n"
      << "  \"target_ceiling\": " << album.target_ceiling << ",\n"
      << "  \"track_count\": " << album.tracks.size() << ",\n"
      << "  \"tracks\": [\n";

  for (std::size_t i = 0; i < album.tracks.size(); ++i) {
    const auto& t = album.tracks[i];
    out << "    {\n"
        << "      \"track_number\": " << (i + 1) << ",\n"
        << "      \"title\": \"" << (t.title.empty() ? t.source_path.stem().string() : t.title) << "\",\n"
        << "      \"file\": \"" << t.master_output_path.filename().string() << "\",\n"
        << "      \"integrated_lufs\": " << t.master_lufs << ",\n"
        << "      \"true_peak_dbtp\": " << t.master_true_peak << "\n"
        << "    }" << (i + 1 < album.tracks.size() ? "," : "") << "\n";
  }

  out << "  ]\n}\n";
  return true;
}

}  // namespace amt::batch
