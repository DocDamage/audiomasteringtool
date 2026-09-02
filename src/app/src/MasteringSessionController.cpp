#include "amt/app/MasteringSessionController.h"

#include <fstream>
#include <system_error>

#include "amt/codec/SndFileCodec.h"
#include "amt/mastering/DesktopMastering.h"
#include "amt/revision/PlanEditor.h"
#include "amt/revision/RevisionExplanation.h"
#include "amt/revision/RevisionParser.h"
#include "amt/translation/TranslationAnalyzer.h"

namespace amt::app {

MasteringSessionController::MasteringSessionController(
    std::shared_ptr<amt::codec::ICodecService> codecs,
    std::shared_ptr<amt::project::ProjectStore> store)
    : codecs_(std::move(codecs)), store_(std::move(store)) {
  if (!codecs_) {
    codecs_ = std::make_shared<amt::codec::SndFileCodecService>();
  }
  if (!store_) {
    store_ = std::make_shared<amt::project::ProjectStore>(
        amt::project::default_project_root());
  }
}

bool MasteringSessionController::open_source(
    const std::filesystem::path& source_path,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (source_path.empty()) {
    error = "No source file provided";
    state_ = AppState::error;
    return false;
  }

  state_ = AppState::loading_file;
  if (!codecs_ || !codecs_->available()) {
    error = "Audio codec service is unavailable";
    state_ = AppState::error;
    return false;
  }

  const auto metadata = codecs_->probe(source_path, error);
  if (!metadata) {
    state_ = AppState::error;
    return false;
  }

  model_ = TrackViewModel{};
  model_.source_path = source_path;
  model_.file_name = source_path.filename().string();
  model_.frames = metadata->frames;
  model_.sample_rate = metadata->sample_rate;
  model_.channels = metadata->channels;
  model_.format_description = metadata->container_name + " / " + metadata->sample_format_name;

  if (store_) {
    std::string store_error;
    auto project = store_->create(source_path, store_error);
    if (store_error.empty()) {
      project_ = std::move(project);
    }
  }

  state_ = AppState::analyzing;
  const auto analysis = amt::analysis::analyze_file(
      *codecs_, source_path, error, cancellation, progress);
  if (!analysis) {
    state_ = AppState::error;
    return false;
  }

  analysis_ = *analysis;
  model_.has_analysis = true;
  model_.integrated_lufs = analysis_->loudness.integrated_lufs;
  model_.true_peak_dbtp = analysis_->loudness.true_peak_dbtp;
  model_.loudness_range_lu = analysis_->loudness.loudness_range_lu;

  state_ = AppState::idle;
  return true;
}

bool MasteringSessionController::run_mastering(
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (!analysis_ || model_.source_path.empty()) {
    error = "Source must be opened and analyzed before mastering";
    state_ = AppState::error;
    return false;
  }

  state_ = AppState::mastering;
  auto plan = amt::mastering::plan_mastering(*analysis_);
  const auto output_dir = store_ && project_
                              ? store_->root() / project_->project_id / "renders"
                              : std::filesystem::temp_directory_path() / "AudioMasteringTool" / "renders";

  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);

  amt::mastering::DesktopMasteringReport report;
  auto rendered = amt::mastering::render_mastering_plan_for_desktop(
      *codecs_, model_.source_path, output_dir, *analysis_, plan, error,
      amt::mastering::RenderSettings{}, cancellation, progress, &report);

  if (!rendered) {
    state_ = AppState::error;
    return false;
  }

  masters_ = *rendered;
  plan_ = std::move(plan);

  model_.has_masters = true;
  model_.master_a_path = masters_->master_a.output_path;
  model_.master_a_lufs = masters_->master_a.analysis.loudness.integrated_lufs;
  model_.master_a_true_peak = masters_->master_a.analysis.loudness.true_peak_dbtp;
  model_.master_a_rationale = plan_->master_a.rationale;

  model_.master_b_path = masters_->master_b.output_path;
  model_.master_b_lufs = masters_->master_b.analysis.loudness.integrated_lufs;
  model_.master_b_true_peak = masters_->master_b.analysis.loudness.true_peak_dbtp;
  model_.master_b_rationale = plan_->master_b.rationale;

  model_.selected_candidate = AuditionTarget::master_a;

  model_.diagnostics.diagnostics_performed = report.source_diagnostics_performed;
  model_.diagnostics.guidance_applied = report.source_guidance_applied;
  model_.diagnostics.automatic_mode1_approved = report.automatic_mode1_approved;
  model_.diagnostics.summary = report.summary;
  model_.diagnostics.raw_json = report.json;

  if (project_ && store_) {
    std::string save_err;
    project_->master_a = {
        .available = true,
        .path = model_.master_a_path,
        .integrated_lufs = model_.master_a_lufs,
        .true_peak_dbtp = model_.master_a_true_peak,
        .recommended = true};
    project_->master_b = {
        .available = true,
        .path = model_.master_b_path,
        .integrated_lufs = model_.master_b_lufs,
        .true_peak_dbtp = model_.master_b_true_peak,
        .recommended = false};
    project_->selected = amt::project::CandidateSelection::master_a;
    store_->append_revision(*project_, "mastering", "Masters rendered",
                            model_.master_a_path, save_err);
  }

  state_ = AppState::ready;
  return true;
}

bool MasteringSessionController::select_candidate(
    AuditionTarget target,
    std::string& error) {
  error.clear();
  if (!model_.has_masters) {
    error = "No mastered candidates available to select";
    return false;
  }
  model_.selected_candidate = target;
  if (project_ && store_) {
    project_->selected = (target == AuditionTarget::master_b)
                             ? amt::project::CandidateSelection::master_b
                             : (target == AuditionTarget::original)
                                   ? amt::project::CandidateSelection::original
                                   : amt::project::CandidateSelection::master_a;
    std::string save_err;
    store_->append_revision(
        *project_, "selection",
        (target == AuditionTarget::master_b)
            ? "Selected Master B"
            : (target == AuditionTarget::original) ? "Selected Original"
                                                   : "Selected Master A",
        (target == AuditionTarget::master_b) ? model_.master_b_path
                                             : model_.master_a_path,
        save_err);
  }
  return true;
}

bool MasteringSessionController::export_selected(
    const std::string& recipe_id,
    const std::filesystem::path& destination_path,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (!model_.has_masters) {
    error = "No mastered candidate to export";
    return false;
  }

  const auto* recipe = amt::project::find_export_recipe(recipe_id);
  if (!recipe || !recipe->available) {
    error = recipe ? recipe->unavailable_reason : "Unknown export recipe: " + recipe_id;
    return false;
  }

  const auto candidate_path = (model_.selected_candidate == AuditionTarget::master_b)
                                  ? model_.master_b_path
                                  : model_.master_a_path;

  state_ = AppState::exporting;
  const auto export_req = amt::project::make_export_request(*recipe);
  const auto result = amt::codec::export_audio(
      *codecs_, candidate_path, destination_path, export_req, error,
      cancellation, progress);

  if (!result) {
    state_ = AppState::ready;
    return false;
  }

  if (project_ && store_) {
    std::string save_err;
    store_->append_revision(*project_, "export", "Exported " + recipe->name,
                            destination_path, save_err);
  }

  state_ = AppState::ready;
  return true;
}

bool MasteringSessionController::apply_revision(
    const std::string& natural_language_prompt,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (!model_.has_masters || !plan_ || !masters_) {
    error = "Mastering must be run before applying natural-language revisions";
    return false;
  }

  amt::revision::RevisionParser parser;
  auto intent = parser.parse(natural_language_prompt);
  if (!intent.parsed_successfully) {
    error = "Could not parse revision intent: " + intent.parse_error;
    return false;
  }

  const auto& target_plan = (model_.selected_candidate == AuditionTarget::master_b)
                                ? plan_->master_b
                                : plan_->master_a;

  auto edit_res = amt::revision::PlanEditor::apply_revision(target_plan, intent);
  if (!edit_res.success) {
    error = edit_res.error;
    return false;
  }

  state_ = AppState::mastering;
  const auto output_dir = store_ && project_
                              ? store_->root() / project_->project_id / "renders"
                              : std::filesystem::temp_directory_path() / "AudioMasteringTool" / "renders";

  const auto rev_output = output_dir / (target_plan.id + "_revised.wav");
  auto rendered = amt::mastering::render_candidate(
      *codecs_, model_.source_path, rev_output, edit_res.revised_plan, error, {}, cancellation, progress);

  if (!rendered) {
    state_ = AppState::ready;
    return false;
  }

  model_.last_revision_prompt = natural_language_prompt;
  model_.last_revision_explanation = amt::revision::RevisionExplanation::generate_explanation(intent, edit_res);

  if (model_.selected_candidate == AuditionTarget::master_b) {
    masters_->master_b = *rendered;
    plan_->master_b = edit_res.revised_plan;
    model_.master_b_path = rendered->output_path;
    model_.master_b_lufs = rendered->analysis.loudness.integrated_lufs;
    model_.master_b_true_peak = rendered->analysis.loudness.true_peak_dbtp;
  } else {
    masters_->master_a = *rendered;
    plan_->master_a = edit_res.revised_plan;
    model_.master_a_path = rendered->output_path;
    model_.master_a_lufs = rendered->analysis.loudness.integrated_lufs;
    model_.master_a_true_peak = rendered->analysis.loudness.true_peak_dbtp;
  }

  if (project_ && store_) {
    std::string save_err;
    store_->append_revision(*project_, "revision", "Applied revision: " + natural_language_prompt,
                            rendered->output_path, save_err);
  }

  state_ = AppState::ready;
  return true;
}

bool MasteringSessionController::run_translation_analysis(std::string& error) {
  error.clear();
  if (!model_.has_masters || !masters_) {
    error = "No mastered audio available for translation simulation";
    return false;
  }

  const auto audio_path = (model_.selected_candidate == AuditionTarget::master_b)
                              ? masters_->master_b.output_path
                              : masters_->master_a.output_path;

  auto decoder = codecs_->open_decoder(audio_path, error);
  if (!decoder) {
    return false;
  }

  amt::audio::AudioBuffer buffer(static_cast<std::size_t>(decoder->metadata().channels),
                                 static_cast<std::size_t>(decoder->metadata().frames));
  std::size_t frames_read = 0;
  if (!decoder->read(buffer, static_cast<std::size_t>(decoder->metadata().frames), frames_read, error)) {
    return false;
  }

  auto report = amt::translation::TranslationAnalyzer::analyze(buffer);
  model_.translation_overall_score = report.overall_score;
  model_.translation_small_speaker_score = report.small_speaker_score;
  model_.translation_mono_score = report.mono_compatibility_score;
  model_.translation_warnings = report.warnings;

  return true;
}

void MasteringSessionController::restore_sidecar_diagnostics(
    const std::filesystem::path& sidecar_json_path) {
  std::ifstream file(sidecar_json_path, std::ios::binary);
  if (!file) return;
  std::string json((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  model_.diagnostics.raw_json = json;
  model_.diagnostics.diagnostics_performed = true;
  if (json.find("\"sourceGuidanceApplied\":true") != std::string::npos) {
    model_.diagnostics.guidance_applied = true;
  }
  if (json.find("\"automaticMode1Approved\":true") != std::string::npos) {
    model_.diagnostics.automatic_mode1_approved = true;
  }
}

}  // namespace amt::app
