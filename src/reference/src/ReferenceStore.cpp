#include "amt/reference/ReferenceStore.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "amt/project/ProjectStore.h"

namespace amt::reference {

ReferenceStore::ReferenceStore(std::filesystem::path root_directory)
    : root_(std::move(root_directory)) {
  if (root_.empty()) {
    root_ = amt::project::default_project_root() / "ReferenceProfiles";
  }
}

bool ReferenceStore::save_profile(
    const ReferenceProfile& profile,
    std::string& error) const {
  error.clear();
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
  if (ec) {
    error = "Could not create reference profile directory: " + ec.message();
    return false;
  }

  const auto target_file = root_ / (profile.profile_id + ".json");
  std::ofstream out(target_file, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "Could not open profile destination for writing";
    return false;
  }

  out << std::setprecision(6)
      << "{\n"
      << "  \"schema_version\": " << profile.schema_version << ",\n"
      << "  \"profile_id\": \"" << profile.profile_id << "\",\n"
      << "  \"display_name\": \"" << profile.display_name << "\",\n"
      << "  \"integrated_lufs\": " << profile.integrated_lufs << ",\n"
      << "  \"true_peak_dbtp\": " << profile.true_peak_dbtp << ",\n"
      << "  \"loudness_range_lu\": " << profile.loudness_range_lu << ",\n"
      << "  \"crest_factor_db\": " << profile.crest_factor_db << ",\n"
      << "  \"stereo_width\": " << profile.stereo_width << ",\n"
      << "  \"spectrum\": {\n"
      << "    \"sub_db\": " << profile.spectrum.sub_db << ",\n"
      << "    \"bass_db\": " << profile.spectrum.bass_db << ",\n"
      << "    \"low_mid_db\": " << profile.spectrum.low_mid_db << ",\n"
      << "    \"mid_db\": " << profile.spectrum.mid_db << ",\n"
      << "    \"high_mid_db\": " << profile.spectrum.high_mid_db << ",\n"
      << "    \"presence_db\": " << profile.spectrum.presence_db << ",\n"
      << "    \"air_db\": " << profile.spectrum.air_db << "\n"
      << "  }\n"
      << "}\n";

  return true;
}

std::optional<ReferenceProfile> ReferenceStore::load_profile(
    const std::filesystem::path& profile_path,
    std::string& error) const {
  error.clear();
  std::ifstream in(profile_path, std::ios::binary);
  if (!in) {
    error = "Could not open profile file";
    return std::nullopt;
  }

  ReferenceProfile profile{};
  profile.profile_id = profile_path.stem().string();
  profile.display_name = profile.profile_id;
  profile.integrated_lufs = -14.0;
  profile.true_peak_dbtp = -1.0;

  // Simple token parser for profile fields
  std::string line;
  while (std::getline(in, line)) {
    if (line.find("\"integrated_lufs\":") != std::string::npos) {
      std::istringstream iss(line.substr(line.find(':') + 1));
      iss >> profile.integrated_lufs;
    } else if (line.find("\"true_peak_dbtp\":") != std::string::npos) {
      std::istringstream iss(line.substr(line.find(':') + 1));
      iss >> profile.true_peak_dbtp;
    } else if (line.find("\"bass_db\":") != std::string::npos) {
      std::istringstream iss(line.substr(line.find(':') + 1));
      iss >> profile.spectrum.bass_db;
    } else if (line.find("\"presence_db\":") != std::string::npos) {
      std::istringstream iss(line.substr(line.find(':') + 1));
      iss >> profile.spectrum.presence_db;
    }
  }

  return profile;
}

bool ReferenceStore::save_my_sound(
    const MySoundProfile& my_sound,
    std::string& error) const {
  error.clear();
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);

  const auto target_file = root_ / "my_sound.json";
  std::ofstream out(target_file, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "Could not open my_sound.json for writing";
    return false;
  }

  out << "{\n"
      << "  \"schema_version\": " << my_sound.schema_version << ",\n"
      << "  \"profile_name\": \"" << my_sound.profile_name << "\",\n"
      << "  \"reference_count\": " << my_sound.reference_count << ",\n"
      << "  \"user_selections_count\": " << my_sound.user_selections_count << ",\n"
      << "  \"integrated_lufs\": " << my_sound.aggregate_profile.integrated_lufs << ",\n"
      << "  \"true_peak_dbtp\": " << my_sound.aggregate_profile.true_peak_dbtp << "\n"
      << "}\n";

  return true;
}

std::optional<MySoundProfile> ReferenceStore::load_my_sound(
    std::string& error) const {
  error.clear();
  const auto target_file = root_ / "my_sound.json";
  std::ifstream in(target_file, std::ios::binary);
  if (!in) {
    error = "my_sound.json not found";
    return std::nullopt;
  }

  MySoundProfile profile{};
  profile.profile_name = "My Sound";
  profile.reference_count = 1;
  profile.aggregate_profile.integrated_lufs = -14.0;
  return profile;
}

MySoundProfile ReferenceStore::aggregate_profiles(
    const std::vector<ReferenceProfile>& profiles,
    const std::string& name) const {
  MySoundProfile agg{};
  agg.profile_name = name;
  agg.reference_count = static_cast<int>(profiles.size());
  if (profiles.empty()) return agg;

  double sum_lufs = 0.0;
  double sum_tp = 0.0;
  double sum_bass = 0.0;
  double sum_presence = 0.0;

  for (const auto& p : profiles) {
    sum_lufs += p.integrated_lufs;
    sum_tp += p.true_peak_dbtp;
    sum_bass += p.spectrum.bass_db;
    sum_presence += p.spectrum.presence_db;
  }

  const double count = static_cast<double>(profiles.size());
  agg.aggregate_profile.integrated_lufs = sum_lufs / count;
  agg.aggregate_profile.true_peak_dbtp = sum_tp / count;
  agg.aggregate_profile.spectrum.bass_db = sum_bass / count;
  agg.aggregate_profile.spectrum.presence_db = sum_presence / count;

  return agg;
}

}  // namespace amt::reference
