#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace amt::project {

enum class CandidateSelection { original, master_a, master_b };

struct CandidateRecord {
  bool available{false};
  std::filesystem::path path;
  double integrated_lufs{-80.0};
  double true_peak_dbtp{-80.0};
  bool recommended{false};
};

struct RevisionNode {
  std::string id;
  std::string parent_id;
  std::int64_t timestamp_ms{0};
  std::string kind;
  std::string summary;
  std::filesystem::path output_path;
};

struct ProjectRecord {
  int schema_version{1};
  std::string project_id;
  std::string display_name;
  std::filesystem::path source_path;
  std::int64_t created_ms{0};
  std::int64_t updated_ms{0};
  CandidateSelection selected{CandidateSelection::original};
  double source_integrated_lufs{-80.0};
  CandidateRecord master_a;
  CandidateRecord master_b;
  std::string analysis_json;
  std::string master_a_graph_json;
  std::string master_b_graph_json;
  std::vector<RevisionNode> revisions;
};

class ProjectStore {
 public:
  explicit ProjectStore(std::filesystem::path root);

  [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
  [[nodiscard]] ProjectRecord create(const std::filesystem::path& source_path,
                                     std::string& error) const;
  bool save(const ProjectRecord& project, std::string& error) const;
  [[nodiscard]] std::optional<ProjectRecord> load(const std::string& project_id,
                                                   std::string& error) const;
  [[nodiscard]] std::optional<ProjectRecord> find_by_source(
      const std::filesystem::path& source_path, std::string& error) const;
  [[nodiscard]] std::vector<ProjectRecord> list_recent(std::size_t limit,
                                                       std::string& error) const;
  bool append_revision(ProjectRecord& project, std::string kind, std::string summary,
                       std::filesystem::path output_path, std::string& error) const;

 private:
  std::filesystem::path root_;
};

[[nodiscard]] std::filesystem::path default_project_root();
[[nodiscard]] std::string selection_name(CandidateSelection selection);
[[nodiscard]] CandidateSelection selection_from_name(const std::string& name);

}  // namespace amt::project
