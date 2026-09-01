#include <cassert>
#include <filesystem>
#include <set>
#include <string>

#include "amt/project/ExportRecipes.h"
#include "amt/project/ProjectStore.h"

namespace {

void test_project_round_trip() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase4-project-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  amt::project::ProjectStore store(root);
  std::string error;
  const std::filesystem::path source = root / std::filesystem::path(u8"Beat – Ω.wav");
  auto project = store.create(source, error);
  assert(error.empty());
  assert(!project.project_id.empty());
  assert(project.source_path == source);
  assert(project.revisions.size() == 1U);
  assert(project.revisions.front().kind == "ingest");

  project.source_integrated_lufs = -14.25;
  project.analysis_json = R"({"schema_version":2,"test":"analysis"})";
  project.master_a_graph_json = R"({"schema_version":1,"nodes":[{"id":"a"}]})";
  project.master_b_graph_json = R"({"schema_version":1,"nodes":[{"id":"b"}]})";
  project.master_a = {.available = true,
                      .path = root / std::filesystem::path(u8"Beat – Ω_Master_A.wav"),
                      .integrated_lufs = -9.8,
                      .true_peak_dbtp = -1.02,
                      .recommended = true};
  project.master_b = {.available = true,
                      .path = root / std::filesystem::path(u8"Beat – Ω_Master_B.wav"),
                      .integrated_lufs = -11.2,
                      .true_peak_dbtp = -1.05,
                      .recommended = false};
  project.selected = amt::project::CandidateSelection::master_b;

  assert(store.append_revision(project, "analysis", "Deep analysis stored.", {}, error));
  assert(store.append_revision(project, "mastering", "Master A and B rendered.",
                               project.master_a.path, error));
  assert(store.append_revision(project, "selection", "Master B selected.",
                               project.master_b.path, error));
  assert(project.revisions.size() == 4U);
  for (std::size_t index = 1U; index < project.revisions.size(); ++index) {
    assert(!project.revisions[index].parent_id.empty());
    assert(project.revisions[index].parent_id == project.revisions[index - 1U].id);
  }

  std::set<std::string> revision_ids;
  for (const auto& revision : project.revisions) revision_ids.insert(revision.id);
  assert(revision_ids.size() == project.revisions.size());

  const auto loaded = store.load(project.project_id, error);
  assert(loaded.has_value());
  assert(loaded->source_path == source);
  assert(loaded->selected == amt::project::CandidateSelection::master_b);
  assert(loaded->analysis_json == project.analysis_json);
  assert(loaded->master_a_graph_json == project.master_a_graph_json);
  assert(loaded->master_b_graph_json == project.master_b_graph_json);
  assert(loaded->master_a.available && loaded->master_b.available);
  assert(loaded->master_a.path == project.master_a.path);
  assert(loaded->master_b.path == project.master_b.path);
  assert(loaded->revisions.size() == project.revisions.size());

  const auto found = store.find_by_source(source, error);
  assert(error.empty());
  assert(found.has_value());
  assert(found->project_id == project.project_id);

  const auto duplicate_create = store.create(source.lexically_normal(), error);
  assert(error.empty());
  assert(duplicate_create.project_id == project.project_id);
  const auto recent = store.list_recent(10U, error);
  assert(recent.size() == 1U);
  assert(recent.front().project_id == project.project_id);

  project.analysis_json.clear();
  project.master_a_graph_json.clear();
  project.master_b_graph_json.clear();
  assert(store.save(project, error));
  const auto cleared = store.load(project.project_id, error);
  assert(cleared.has_value());
  assert(cleared->analysis_json.empty());
  assert(cleared->master_a_graph_json.empty());
  assert(cleared->master_b_graph_json.empty());

  std::filesystem::remove_all(root, ignored);
}

void test_selection_history_and_stale_snapshot_merge() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase4-selection-history-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  amt::project::ProjectStore store(root);
  std::string error;
  auto project = store.create(root / "selection.wav", error);
  assert(error.empty());
  project.master_a = {.available = true,
                      .path = root / "selection_Master_A.wav",
                      .integrated_lufs = -9.0,
                      .true_peak_dbtp = -1.0,
                      .recommended = true};
  project.master_b = {.available = true,
                      .path = root / "selection_Master_B.wav",
                      .integrated_lufs = -11.0,
                      .true_peak_dbtp = -1.0,
                      .recommended = false};
  assert(store.append_revision(project, "mastering", "Two candidates rendered.",
                               project.master_a.path, error));

  const auto before_selection_count = project.revisions.size();
  project.selected = amt::project::CandidateSelection::master_a;
  assert(store.save(project, error));
  const auto selected = store.load(project.project_id, error);
  assert(selected.has_value());
  assert(selected->selected == amt::project::CandidateSelection::master_a);
  assert(selected->revisions.size() == before_selection_count + 1U);
  assert(selected->revisions.back().kind == "selection");
  assert(selected->revisions.back().parent_id ==
         selected->revisions[selected->revisions.size() - 2U].id);
  assert(selected->revisions.back().output_path == project.master_a.path);

  // Simulate the desktop holding the pre-selection revision vector after save().
  // append_revision() must merge the persisted selection node instead of overwriting it.
  assert(project.revisions.size() == before_selection_count);
  assert(store.append_revision(project, "export", "Exported selected Master A.",
                               project.master_a.path, error));
  assert(project.revisions.size() == before_selection_count + 2U);
  assert(project.revisions[project.revisions.size() - 2U].kind == "selection");
  assert(project.revisions.back().kind == "export");
  assert(project.revisions.back().parent_id ==
         project.revisions[project.revisions.size() - 2U].id);

  project.selected = amt::project::CandidateSelection::master_b;
  assert(store.save(project, error));
  const auto selected_b = store.load(project.project_id, error);
  assert(selected_b.has_value());
  assert(selected_b->selected == amt::project::CandidateSelection::master_b);
  assert(selected_b->revisions.back().kind == "selection");
  assert(selected_b->revisions.back().output_path == project.master_b.path);

  std::filesystem::remove_all(root, ignored);
}

void test_revision_id_uniqueness() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase4-revision-id-tests";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  amt::project::ProjectStore store(root);
  std::string error;
  auto project = store.create(root / "rapid.wav", error);
  assert(!project.project_id.empty());

  for (int index = 0; index < 16; ++index) {
    assert(store.append_revision(project, "test", "Rapid revision.", {}, error));
  }
  std::set<std::string> ids;
  for (const auto& revision : project.revisions) ids.insert(revision.id);
  assert(ids.size() == project.revisions.size());
  for (std::size_t index = 1U; index < project.revisions.size(); ++index) {
    assert(project.revisions[index].parent_id == project.revisions[index - 1U].id);
  }

  std::filesystem::remove_all(root, ignored);
}

void test_export_recipes() {
  const auto& recipes = amt::project::builtin_export_recipes();
  assert(recipes.size() >= 6U);

  const auto* studio = amt::project::find_export_recipe("studio_master");
  assert(studio != nullptr && studio->available);
  assert(studio->container == amt::codec::AudioContainer::wav);
  assert(studio->sample_format == amt::codec::AudioSampleFormat::pcm24);
  assert(!studio->sample_rate.has_value());

  const auto* distribution_wav = amt::project::find_export_recipe("distribution_wav");
  assert(distribution_wav != nullptr && distribution_wav->available);
  assert(distribution_wav->container == amt::codec::AudioContainer::wav);
  assert(distribution_wav->sample_format == amt::codec::AudioSampleFormat::pcm24);

  const auto* distribution_flac = amt::project::find_export_recipe("distribution_flac");
  assert(distribution_flac != nullptr && distribution_flac->available);
  assert(distribution_flac->container == amt::codec::AudioContainer::flac);
  assert(distribution_flac->sample_format == amt::codec::AudioSampleFormat::pcm24);

  const auto* cd = amt::project::find_export_recipe(amt::project::ExportRecipeId::cd);
  assert(cd != nullptr && cd->available);
  assert(cd->sample_rate == 44100);
  assert(cd->sample_format == amt::codec::AudioSampleFormat::pcm16);
  const auto cd_request = amt::project::make_export_request(*cd);
  assert(cd_request.sample_rate == 44100);
  assert(cd_request.container == amt::codec::AudioContainer::wav);
  assert(cd_request.sample_format == amt::codec::AudioSampleFormat::pcm16);
  assert(cd_request.dither_when_reducing_integer_depth);

  const auto* preview = amt::project::find_export_recipe("client_preview");
  assert(preview != nullptr);
  assert(!preview->available);
  assert(preview->container == amt::codec::AudioContainer::mp3);
  assert(!preview->unavailable_reason.empty());

  const auto* archive = amt::project::find_export_recipe("archive_float");
  assert(archive != nullptr && archive->available);
  assert(archive->container == amt::codec::AudioContainer::wav);
  assert(archive->sample_format == amt::codec::AudioSampleFormat::float32);
  assert(!archive->dither_when_reducing_integer_depth);
}

}  // namespace

int main() {
  test_project_round_trip();
  test_selection_history_and_stale_snapshot_merge();
  test_revision_id_uniqueness();
  test_export_recipes();
  return 0;
}
