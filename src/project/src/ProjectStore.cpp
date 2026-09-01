#include "amt/project/ProjectStore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace amt::project {
namespace {

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string path_to_utf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::filesystem::path path_from_utf8(const std::string& value) {
  std::u8string converted;
  converted.reserve(value.size());
  for (const unsigned char byte : value) converted.push_back(static_cast<char8_t>(byte));
  return std::filesystem::path(converted);
}

std::filesystem::path normalized_source_path(const std::filesystem::path& path) {
  std::error_code ec;
  auto normalized = std::filesystem::weakly_canonical(path, ec);
  if (!ec) return normalized.lexically_normal();

  ec.clear();
  normalized = std::filesystem::absolute(path, ec);
  if (!ec) return normalized.lexically_normal();
  return path.lexically_normal();
}

bool source_paths_match(const std::filesystem::path& first,
                        const std::filesystem::path& second) {
  std::error_code first_exists_error;
  const bool first_exists = std::filesystem::exists(first, first_exists_error);
  std::error_code second_exists_error;
  const bool second_exists = std::filesystem::exists(second, second_exists_error);
  if (!first_exists_error && !second_exists_error && first_exists && second_exists) {
    std::error_code equivalent_error;
    if (std::filesystem::equivalent(first, second, equivalent_error) && !equivalent_error) return true;
  }

  const auto normalized_first = normalized_source_path(first);
  const auto normalized_second = normalized_source_path(second);
#ifdef _WIN32
  auto first_native = normalized_first.native();
  auto second_native = normalized_second.native();
  std::transform(first_native.begin(), first_native.end(), first_native.begin(),
                 [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
  std::transform(second_native.begin(), second_native.end(), second_native.begin(),
                 [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
  return first_native == second_native;
#else
  return normalized_first == normalized_second;
#endif
}

std::string hex_encode(const std::string_view value) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string output;
  output.reserve(value.size() * 2U);
  for (const unsigned char byte : value) {
    output.push_back(digits[byte >> 4U]);
    output.push_back(digits[byte & 0x0FU]);
  }
  return output;
}

int hex_value(const char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

std::optional<std::string> hex_decode(const std::string& value) {
  if (value.size() % 2U != 0U) return std::nullopt;
  std::string output;
  output.reserve(value.size() / 2U);
  for (std::size_t index = 0U; index < value.size(); index += 2U) {
    const int high = hex_value(value[index]);
    const int low = hex_value(value[index + 1U]);
    if (high < 0 || low < 0) return std::nullopt;
    output.push_back(static_cast<char>((high << 4) | low));
  }
  return output;
}

std::string encode_path(const std::filesystem::path& path) {
  return hex_encode(path_to_utf8(path));
}

std::filesystem::path decode_path(const std::string& value) {
  const auto decoded = hex_decode(value);
  return decoded ? path_from_utf8(*decoded) : std::filesystem::path{};
}

std::string make_id(const std::filesystem::path& source) {
  static std::atomic_uint64_t sequence{0U};
  const auto timestamp = now_ms();
  const auto serial = sequence.fetch_add(1U, std::memory_order_relaxed);
  const auto source_text = path_to_utf8(source);

  std::random_device device;
  const auto entropy = (static_cast<std::uint64_t>(device()) << 32U) ^
                       static_cast<std::uint64_t>(device());
  const auto hash = std::hash<std::string>{}(
      source_text + ':' + std::to_string(timestamp) + ':' + std::to_string(serial));
  const auto suffix = static_cast<std::uint64_t>(hash) ^ entropy ^ (serial * 0x9E3779B97F4A7C15ULL);

  std::ostringstream stream;
  stream << "amt-" << timestamp << '-' << std::hex << std::setw(8) << std::setfill('0')
         << serial << '-' << std::setw(16) << suffix;
  return stream.str();
}

bool revisions_are_prefix(const std::vector<RevisionNode>& prefix,
                          const std::vector<RevisionNode>& history) {
  if (prefix.size() > history.size()) return false;
  for (std::size_t index = 0U; index < prefix.size(); ++index) {
    if (prefix[index].id != history[index].id) return false;
  }
  return true;
}

std::filesystem::path selected_output_path(const ProjectRecord& project) {
  switch (project.selected) {
    case CandidateSelection::master_a:
      return project.master_a.available ? project.master_a.path : std::filesystem::path{};
    case CandidateSelection::master_b:
      return project.master_b.available ? project.master_b.path : std::filesystem::path{};
    case CandidateSelection::original:
      return project.source_path;
  }
  return project.source_path;
}

std::string selection_summary(const CandidateSelection selection) {
  switch (selection) {
    case CandidateSelection::master_a: return "Master A selected for audition/export.";
    case CandidateSelection::master_b: return "Master B selected for audition/export.";
    case CandidateSelection::original: return "Original selected for audition/export.";
  }
  return "Original selected for audition/export.";
}

void append_selection_revision(ProjectRecord& project) {
  project.updated_ms = now_ms();
  const std::string parent = project.revisions.empty() ? std::string{} : project.revisions.back().id;
  project.revisions.push_back({.id = make_id(project.source_path),
                               .parent_id = parent,
                               .timestamp_ms = project.updated_ms,
                               .kind = "selection",
                               .summary = selection_summary(project.selected),
                               .output_path = selected_output_path(project)});
}

bool write_atomic(const std::filesystem::path& path, const std::string& content,
                  std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "unable to create project directory: " + ec.message();
    return false;
  }

  std::filesystem::path temporary = path;
  temporary += ".tmp";
  std::filesystem::path backup = path;
  backup += ".bak";
  std::filesystem::remove(temporary, ec);
  ec.clear();
  std::filesystem::remove(backup, ec);
  ec.clear();

  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "unable to open temporary project file for writing";
      return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
      error = "failed while writing project data";
      return false;
    }
  }

  const bool had_existing = std::filesystem::exists(path, ec) && !ec;
  ec.clear();
  if (had_existing) {
    std::filesystem::rename(path, backup, ec);
    if (ec) {
      error = "unable to stage existing project file: " + ec.message();
      std::filesystem::remove(temporary, ec);
      return false;
    }
  }

  ec.clear();
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    const std::string message = ec.message();
    if (had_existing) {
      std::error_code rollback_error;
      std::filesystem::rename(backup, path, rollback_error);
    }
    std::filesystem::remove(temporary, ec);
    error = "unable to finalize project file: " + message;
    return false;
  }

  if (had_existing) {
    ec.clear();
    std::filesystem::remove(backup, ec);
  }
  return true;
}

bool remove_stale_sidecar(const std::filesystem::path& path, std::string& error) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
  if (ec) {
    error = "unable to remove stale project sidecar: " + ec.message();
    return false;
  }
  return true;
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return std::nullopt;
  std::ostringstream stream;
  stream << input.rdbuf();
  return stream.str();
}

std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0U;
  while (start <= line.size()) {
    const auto end = line.find('\t', start);
    if (end == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, end - start));
    start = end + 1U;
  }
  return fields;
}

std::optional<double> parse_double(const std::string& value) {
  try {
    std::size_t consumed = 0U;
    const double parsed = std::stod(value, &consumed);
    return consumed == value.size() ? std::optional<double>(parsed) : std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::int64_t> parse_int64(const std::string& value) {
  try {
    std::size_t consumed = 0U;
    const auto parsed = std::stoll(value, &consumed);
    return consumed == value.size() ? std::optional<std::int64_t>(parsed) : std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

std::string manifest_text(const ProjectRecord& project) {
  std::ostringstream output;
  output << std::setprecision(17)
         << "schema=" << project.schema_version << '\n'
         << "project_id=" << hex_encode(project.project_id) << '\n'
         << "display_name=" << hex_encode(project.display_name) << '\n'
         << "source_path=" << encode_path(project.source_path) << '\n'
         << "created_ms=" << project.created_ms << '\n'
         << "updated_ms=" << project.updated_ms << '\n'
         << "selected=" << selection_name(project.selected) << '\n'
         << "source_lufs=" << project.source_integrated_lufs << '\n'
         << "master_a_available=" << (project.master_a.available ? 1 : 0) << '\n'
         << "master_a_path=" << encode_path(project.master_a.path) << '\n'
         << "master_a_lufs=" << project.master_a.integrated_lufs << '\n'
         << "master_a_dbtp=" << project.master_a.true_peak_dbtp << '\n'
         << "master_a_recommended=" << (project.master_a.recommended ? 1 : 0) << '\n'
         << "master_b_available=" << (project.master_b.available ? 1 : 0) << '\n'
         << "master_b_path=" << encode_path(project.master_b.path) << '\n'
         << "master_b_lufs=" << project.master_b.integrated_lufs << '\n'
         << "master_b_dbtp=" << project.master_b.true_peak_dbtp << '\n'
         << "master_b_recommended=" << (project.master_b.recommended ? 1 : 0) << '\n';
  return output.str();
}

std::string revisions_text(const ProjectRecord& project) {
  std::ostringstream output;
  for (const auto& revision : project.revisions) {
    output << hex_encode(revision.id) << '\t' << hex_encode(revision.parent_id) << '\t'
           << revision.timestamp_ms << '\t' << hex_encode(revision.kind) << '\t'
           << hex_encode(revision.summary) << '\t' << encode_path(revision.output_path) << '\n';
  }
  return output.str();
}

}  // namespace

ProjectStore::ProjectStore(std::filesystem::path root) : root_(std::move(root)) {}

ProjectRecord ProjectStore::create(const std::filesystem::path& source_path,
                                   std::string& error) const {
  error.clear();
  if (source_path.empty()) {
    error = "source path is empty";
    return {};
  }

  std::string lookup_error;
  if (auto existing = find_by_source(source_path, lookup_error)) return *existing;
  if (!lookup_error.empty()) {
    error = std::move(lookup_error);
    return {};
  }

  const auto canonical_source = normalized_source_path(source_path);
  ProjectRecord project;
  project.project_id = make_id(canonical_source);
  project.display_name = path_to_utf8(canonical_source.stem());
  project.source_path = canonical_source;
  project.created_ms = now_ms();
  project.updated_ms = project.created_ms;
  project.revisions.push_back({.id = make_id(canonical_source),
                               .parent_id = {},
                               .timestamp_ms = project.created_ms,
                               .kind = "ingest",
                               .summary = "Source added to project history.",
                               .output_path = {}});
  if (!save(project, error)) return {};
  return project;
}

bool ProjectStore::save(const ProjectRecord& project, std::string& error) const {
  error.clear();
  if (project.project_id.empty() || project.source_path.empty()) {
    error = "project record is missing its id or source path";
    return false;
  }
  const auto directory = root_ / project.project_id;
  const auto manifest_path = directory / "manifest.amt";
  ProjectRecord persisted = project;

  std::error_code exists_error;
  const bool manifest_exists = std::filesystem::exists(manifest_path, exists_error);
  if (exists_error) {
    error = "unable to inspect existing project manifest: " + exists_error.message();
    return false;
  }
  if (manifest_exists) {
    std::string load_error;
    const auto existing = load(project.project_id, load_error);
    if (!existing) {
      error = "unable to safely update existing project: " + load_error;
      return false;
    }

    const bool disk_is_prefix = revisions_are_prefix(existing->revisions, project.revisions);
    const bool caller_is_prefix = revisions_are_prefix(project.revisions, existing->revisions);
    if (!disk_is_prefix && !caller_is_prefix) {
      error = "project history conflict; reload the project before saving";
      return false;
    }

    const bool caller_added_revision =
        disk_is_prefix && project.revisions.size() > existing->revisions.size();
    if (caller_is_prefix && existing->revisions.size() > project.revisions.size()) {
      persisted.revisions = existing->revisions;
      persisted.updated_ms = std::max(persisted.updated_ms, existing->updated_ms);
    }

    if (existing->selected != project.selected && !caller_added_revision) {
      append_selection_revision(persisted);
    }
  }

  if (!persisted.analysis_json.empty()) {
    if (!write_atomic(directory / "analysis-v2.json", persisted.analysis_json, error)) return false;
  } else if (!remove_stale_sidecar(directory / "analysis-v2.json", error)) {
    return false;
  }

  if (!persisted.master_a_graph_json.empty()) {
    if (!write_atomic(directory / "master-a-graph.json", persisted.master_a_graph_json, error)) return false;
  } else if (!remove_stale_sidecar(directory / "master-a-graph.json", error)) {
    return false;
  }

  if (!persisted.master_b_graph_json.empty()) {
    if (!write_atomic(directory / "master-b-graph.json", persisted.master_b_graph_json, error)) return false;
  } else if (!remove_stale_sidecar(directory / "master-b-graph.json", error)) {
    return false;
  }

  if (!write_atomic(directory / "revisions.amtlog", revisions_text(persisted), error)) return false;
  return write_atomic(manifest_path, manifest_text(persisted), error);
}

std::optional<ProjectRecord> ProjectStore::load(const std::string& project_id,
                                                 std::string& error) const {
  error.clear();
  const auto directory = root_ / project_id;
  const auto manifest = read_text(directory / "manifest.amt");
  if (!manifest) {
    error = "project manifest not found";
    return std::nullopt;
  }
  ProjectRecord project;
  std::istringstream lines(*manifest);
  std::string line;
  while (std::getline(lines, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos) continue;
    const std::string key = line.substr(0U, separator);
    const std::string value = line.substr(separator + 1U);
    if (key == "schema") {
      const auto parsed = parse_int64(value);
      if (parsed) project.schema_version = static_cast<int>(*parsed);
    } else if (key == "project_id") {
      if (const auto decoded = hex_decode(value)) project.project_id = *decoded;
    } else if (key == "display_name") {
      if (const auto decoded = hex_decode(value)) project.display_name = *decoded;
    } else if (key == "source_path") project.source_path = decode_path(value);
    else if (key == "created_ms") { if (const auto parsed = parse_int64(value)) project.created_ms = *parsed; }
    else if (key == "updated_ms") { if (const auto parsed = parse_int64(value)) project.updated_ms = *parsed; }
    else if (key == "selected") project.selected = selection_from_name(value);
    else if (key == "source_lufs") { if (const auto parsed = parse_double(value)) project.source_integrated_lufs = *parsed; }
    else if (key == "master_a_available") project.master_a.available = value == "1";
    else if (key == "master_a_path") project.master_a.path = decode_path(value);
    else if (key == "master_a_lufs") { if (const auto parsed = parse_double(value)) project.master_a.integrated_lufs = *parsed; }
    else if (key == "master_a_dbtp") { if (const auto parsed = parse_double(value)) project.master_a.true_peak_dbtp = *parsed; }
    else if (key == "master_a_recommended") project.master_a.recommended = value == "1";
    else if (key == "master_b_available") project.master_b.available = value == "1";
    else if (key == "master_b_path") project.master_b.path = decode_path(value);
    else if (key == "master_b_lufs") { if (const auto parsed = parse_double(value)) project.master_b.integrated_lufs = *parsed; }
    else if (key == "master_b_dbtp") { if (const auto parsed = parse_double(value)) project.master_b.true_peak_dbtp = *parsed; }
    else if (key == "master_b_recommended") project.master_b.recommended = value == "1";
  }
  if (project.schema_version != 1 || project.project_id.empty() || project.source_path.empty()) {
    error = "project manifest is invalid or unsupported";
    return std::nullopt;
  }
  if (const auto analysis = read_text(directory / "analysis-v2.json")) project.analysis_json = *analysis;
  if (const auto graph = read_text(directory / "master-a-graph.json")) project.master_a_graph_json = *graph;
  if (const auto graph = read_text(directory / "master-b-graph.json")) project.master_b_graph_json = *graph;

  if (const auto revision_text = read_text(directory / "revisions.amtlog")) {
    std::istringstream revision_lines(*revision_text);
    while (std::getline(revision_lines, line)) {
      const auto fields = split_tabs(line);
      if (fields.size() != 6U) continue;
      const auto id = hex_decode(fields[0]);
      const auto parent = hex_decode(fields[1]);
      const auto timestamp = parse_int64(fields[2]);
      const auto kind = hex_decode(fields[3]);
      const auto summary = hex_decode(fields[4]);
      if (!id || !parent || !timestamp || !kind || !summary) continue;
      project.revisions.push_back({.id = *id, .parent_id = *parent,
                                   .timestamp_ms = *timestamp, .kind = *kind,
                                   .summary = *summary, .output_path = decode_path(fields[5])});
    }
  }
  return project;
}

std::optional<ProjectRecord> ProjectStore::find_by_source(
    const std::filesystem::path& source_path, std::string& error) const {
  error.clear();
  if (source_path.empty()) return std::nullopt;

  std::error_code ec;
  if (!std::filesystem::exists(root_, ec)) {
    if (ec) error = "unable to inspect project history: " + ec.message();
    return std::nullopt;
  }

  std::optional<ProjectRecord> newest_match;
  for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
    if (ec) break;
    if (!entry.is_directory()) continue;
    std::string load_error;
    auto project = load(entry.path().filename().string(), load_error);
    if (!project || !source_paths_match(project->source_path, source_path)) continue;
    if (!newest_match || project->updated_ms > newest_match->updated_ms) {
      newest_match = std::move(*project);
    }
  }
  if (ec) error = "unable to enumerate project history: " + ec.message();
  return newest_match;
}

std::vector<ProjectRecord> ProjectStore::list_recent(const std::size_t limit,
                                                     std::string& error) const {
  error.clear();
  std::vector<ProjectRecord> projects;
  std::error_code ec;
  if (!std::filesystem::exists(root_, ec)) {
    if (ec) error = "unable to inspect project history: " + ec.message();
    return projects;
  }
  for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
    if (ec) break;
    if (!entry.is_directory()) continue;
    std::string load_error;
    if (auto project = load(entry.path().filename().string(), load_error)) {
      projects.push_back(std::move(*project));
    }
  }
  if (ec) {
    error = "unable to enumerate project history: " + ec.message();
    return {};
  }
  std::sort(projects.begin(), projects.end(), [](const ProjectRecord& a, const ProjectRecord& b) {
    return a.updated_ms > b.updated_ms;
  });
  if (projects.size() > limit) projects.resize(limit);
  return projects;
}

bool ProjectStore::append_revision(ProjectRecord& project, std::string kind,
                                   std::string summary, std::filesystem::path output_path,
                                   std::string& error) const {
  error.clear();

  std::string load_error;
  if (const auto existing = load(project.project_id, load_error)) {
    const bool disk_is_prefix = revisions_are_prefix(existing->revisions, project.revisions);
    const bool caller_is_prefix = revisions_are_prefix(project.revisions, existing->revisions);
    if (!disk_is_prefix && !caller_is_prefix) {
      error = "project history conflict; reload the project before appending a revision";
      return false;
    }
    if (caller_is_prefix && existing->revisions.size() > project.revisions.size()) {
      project.revisions = existing->revisions;
      project.updated_ms = std::max(project.updated_ms, existing->updated_ms);
    }
  } else {
    std::error_code exists_error;
    const bool manifest_exists = std::filesystem::exists(root_ / project.project_id / "manifest.amt",
                                                         exists_error);
    if (exists_error) {
      error = "unable to inspect existing project manifest: " + exists_error.message();
      return false;
    }
    if (manifest_exists) {
      error = "unable to safely append to existing project: " + load_error;
      return false;
    }
  }

  const auto previous_updated_ms = project.updated_ms;
  const auto previous_revision_count = project.revisions.size();
  const std::string parent = project.revisions.empty() ? std::string{} : project.revisions.back().id;
  project.updated_ms = now_ms();
  project.revisions.push_back({.id = make_id(project.source_path),
                               .parent_id = parent,
                               .timestamp_ms = project.updated_ms,
                               .kind = std::move(kind),
                               .summary = std::move(summary),
                               .output_path = std::move(output_path)});
  if (save(project, error)) return true;

  project.updated_ms = previous_updated_ms;
  project.revisions.resize(previous_revision_count);
  return false;
}

std::filesystem::path default_project_root() {
#ifdef _WIN32
  if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"); local != nullptr && local[0] != L'\0') {
    return std::filesystem::path(local) / L"AudioMasteringTool" / L"Projects";
  }
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "AudioMasteringTool" / "Projects";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return std::filesystem::path(home) / ".local" / "share" / "AudioMasteringTool" / "Projects";
  }
#endif
  return std::filesystem::temp_directory_path() / "AudioMasteringTool" / "Projects";
}

std::string selection_name(const CandidateSelection selection) {
  switch (selection) {
    case CandidateSelection::original: return "original";
    case CandidateSelection::master_a: return "master_a";
    case CandidateSelection::master_b: return "master_b";
  }
  return "original";
}

CandidateSelection selection_from_name(const std::string& name) {
  if (name == "master_a") return CandidateSelection::master_a;
  if (name == "master_b") return CandidateSelection::master_b;
  return CandidateSelection::original;
}

}  // namespace amt::project
