#ifdef _WIN32

#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "amt/analysis/DeepAnalysis.h"
#include "amt/batch/BatchExport.h"
#include "amt/batch/BatchProject.h"
#include "amt/batch/BatchQueue.h"
#include "amt/batch/CohesionPlanner.h"
#include "amt/batch/CollectionAnalysis.h"
#include "amt/codec/AudioIO.h"
#include "amt/codec/SndFileCodec.h"
#include "amt/core/JobControl.h"
#include "amt/core/Version.h"
#include "amt/mastering/Audition.h"
#include "amt/mastering/OfflineRenderer.h"
#include "amt/mastering/Planner.h"
#include "amt/playback/ComparisonTransport.h"
#include "amt/playback/Transport.h"
#include "amt/project/ExportRecipes.h"
#include "amt/project/ProjectStore.h"
#include "amt/revision/ConstraintResolver.h"
#include "amt/revision/PlanEditor.h"
#include "amt/revision/RevisionExplanation.h"
#include "amt/revision/RevisionParser.h"
#include "amt/settings/CacheManager.h"
#include "amt/settings/CrashReporting.h"
#include "amt/settings/ModelManager.h"
#include "amt/settings/SettingsManager.h"
#include "amt/translation/PlaybackClass.h"
#include "amt/translation/TranslationAnalyzer.h"
#include "amt/translation/TranslationModel.h"

namespace {

constexpr wchar_t kWindowClass[] = L"AudioMasteringToolPhase4Window";
constexpr UINT_PTR kUiTimer = 1U;
constexpr int kSeekRange = 10000;
constexpr UINT kAnalysisFinished = WM_APP + 1U;
constexpr UINT kMasterFinished = WM_APP + 2U;
constexpr UINT kExportFinished = WM_APP + 3U;
constexpr UINT kJobProgress = WM_APP + 4U;
constexpr UINT kRevisionFinished = WM_APP + 5U;
constexpr UINT kBatchFinished = WM_APP + 6U;

constexpr int kOpenId = 1001;
constexpr int kRecentId = 1002;
constexpr int kAnalyzeId = 1003;
constexpr int kMasterId = 1004;
constexpr int kExportId = 1005;
constexpr int kCancelId = 1006;
constexpr int kPlayId = 1010;
constexpr int kStopId = 1011;
constexpr int kOriginalId = 1012;
constexpr int kMasterAId = 1013;
constexpr int kMasterBId = 1014;
constexpr int kRecipeId = 1020;
constexpr int kSeekId = 1101;
constexpr int kRevisionEditId = 1201;
constexpr int kRevisionButtonId = 1202;
constexpr int kTranslationComboId = 1210;
constexpr int kBatchButtonId = 1230;
constexpr int kSettingsButtonId = 1240;
constexpr int kStyleComboId = 1250;
constexpr int kTargetSliderId = 1251;
constexpr int kBassSliderId = 1252;
constexpr int kPresenceSliderId = 1253;
constexpr int kWidthSliderId = 1254;
constexpr int kPunchSliderId = 1255;
constexpr int kWarmthSliderId = 1256;
constexpr int kDrumsSliderId = 1260;
constexpr int kStemBassSliderId = 1261;
constexpr int kVocalsSliderId = 1262;
constexpr int kOtherSliderId = 1263;
constexpr UINT kRecentMenuBase = 40000U;

LONG WINAPI record_unhandled_exception(EXCEPTION_POINTERS* exception) {
  amt::settings::SettingsManager settings;
  std::string settings_error;
  settings.load(settings_error);
  if (!settings.settings().crash_reports_enabled) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  std::ostringstream details;
  details << "Unhandled Windows exception";
  if (exception != nullptr && exception->ExceptionRecord != nullptr) {
    details << " code=0x" << std::hex
            << exception->ExceptionRecord->ExceptionCode;
  }
  std::string crash_error;
  amt::settings::CrashReporting::record_crash_log(
      amt::settings::SettingsManager::default_settings_path().parent_path() /
          "crashes",
      "AudioMasteringTool desktop process", details.str(), true, crash_error);
  return EXCEPTION_CONTINUE_SEARCH;
}

HMENU control_menu(const int id) noexcept {
  return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

std::wstring widen_utf8(const std::string& text) {
  if (text.empty()) return {};
  const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) return std::wstring(text.begin(), text.end());
  std::wstring output(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      output.data(), count);
  return output;
}

std::string narrow_utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (count <= 0) return {};
  std::string output(static_cast<std::size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      output.data(), count, nullptr, nullptr);
  return output;
}

std::string path_utf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

struct AppState {
  amt::codec::SndFileCodecService codecs;
  amt::playback::Transport transport{codecs};
  amt::playback::ComparisonTransport comparison{codecs};
  amt::project::ProjectStore projects{amt::project::default_project_root()};

  HWND window{nullptr};
  HWND open_button{nullptr};
  HWND recent_button{nullptr};
  HWND analyze_button{nullptr};
  HWND master_button{nullptr};
  HWND export_button{nullptr};
  HWND cancel_button{nullptr};
  HWND settings_button{nullptr};
  HWND batch_button{nullptr};
  HWND play_button{nullptr};
  HWND stop_button{nullptr};
  HWND original_button{nullptr};
  HWND master_a_button{nullptr};
  HWND master_b_button{nullptr};
  HWND translation_combo{nullptr};
  HWND revision_edit{nullptr};
  HWND revision_button{nullptr};
  HWND recipe_combo{nullptr};
  HWND seek{nullptr};
  HWND progress{nullptr};
  HWND status{nullptr};
  HWND details{nullptr};
  HWND style_label{nullptr};
  HWND style_combo{nullptr};
  std::array<HWND, 6> mastering_labels{};
  std::array<HWND, 6> mastering_sliders{};
  std::array<HWND, 4> stem_mix_labels{};
  std::array<HWND, 4> stem_mix_sliders{};

  std::filesystem::path source_path;
  std::optional<amt::codec::AudioMetadata> metadata;
  std::optional<amt::analysis::AnalysisReport> analysis;
  std::optional<amt::mastering::MasteringPlan> plan;
  std::optional<amt::mastering::MasteringRenderPair> rendered;
  std::optional<amt::project::ProjectRecord> project;

  std::string last_revision_explanation;
  amt::translation::PlaybackClassId active_translation{amt::translation::PlaybackClassId::studio_monitors};

  std::mutex data_mutex;
  std::thread worker;
  std::shared_ptr<amt::core::CancellationToken> cancellation;
  std::atomic_bool busy{false};
  std::string worker_error;
  bool playable{false};
  bool comparison_ready{false};
  int last_seek_position{-1};
  amt::mastering::MasteringControls mastering_controls =
      amt::mastering::mastering_style_preset(
          amt::mastering::MasteringStyle::balanced);

  ~AppState() {
    if (cancellation) cancellation->cancel();
    if (worker.joinable()) worker.join();
    comparison.stop();
    transport.stop();
  }
};

AppState* state_from(HWND window) {
  return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

void set_status(AppState& state, const std::wstring& text) {
  SetWindowTextW(state.status, text.c_str());
}

void show_error(HWND window, const wchar_t* title, const std::string& error) {
  const auto wide = widen_utf8(error.empty() ? "Unknown error." : error);
  MessageBoxW(window, wide.c_str(), title, MB_OK | MB_ICONERROR);
}

void join_worker_if_done(AppState& state) {
  if (!state.busy.load(std::memory_order_acquire) && state.worker.joinable()) state.worker.join();
}

amt::playback::TransportState transport_state(const AppState& state) {
  return state.comparison_ready ? state.comparison.state() : state.transport.state();
}

std::int64_t playhead_frame(const AppState& state) {
  return state.comparison_ready ? state.comparison.playhead_frame() : state.transport.playhead_frame();
}

void stop_playback(AppState& state) {
  if (state.comparison_ready) state.comparison.stop();
  else state.transport.stop();
}

std::wstring selection_label(const amt::project::CandidateSelection selection) {
  switch (selection) {
    case amt::project::CandidateSelection::master_a: return L"Master A";
    case amt::project::CandidateSelection::master_b: return L"Master B";
    case amt::project::CandidateSelection::original: return L"Original";
  }
  return L"Original";
}

amt::playback::ComparisonSource comparison_source(
    const amt::project::CandidateSelection selection) {
  switch (selection) {
    case amt::project::CandidateSelection::master_a:
      return amt::playback::ComparisonSource::master_a;
    case amt::project::CandidateSelection::master_b:
      return amt::playback::ComparisonSource::master_b;
    case amt::project::CandidateSelection::original:
      return amt::playback::ComparisonSource::original;
  }
  return amt::playback::ComparisonSource::original;
}

void update_play_button(AppState& state) {
  const wchar_t* text = L"Play";
  if (transport_state(state) == amt::playback::TransportState::playing) text = L"Pause";
  else if (transport_state(state) == amt::playback::TransportState::paused) text = L"Resume";
  wchar_t current[16]{};
  GetWindowTextW(state.play_button, current, static_cast<int>(std::size(current)));
  if (std::wstring(current) != text) SetWindowTextW(state.play_button, text);
}

void update_mastering_control_labels(AppState& state) {
  const auto& controls = state.mastering_controls;
  const std::array<std::wstring, 6> labels = {
      L"Loudness  " + std::to_wstring(controls.target_lufs).substr(0, 5) + L" LUFS",
      L"Bass  " + std::to_wstring(controls.bass_db).substr(0, 4) + L" dB",
      L"Presence  " + std::to_wstring(controls.presence_db).substr(0, 4) + L" dB",
      L"Width  " + std::to_wstring(static_cast<int>(std::lround(controls.width * 100.0))) + L"%",
      L"Punch  " + std::to_wstring(static_cast<int>(std::lround(controls.punch * 100.0))) + L"%",
      L"Warmth  " + std::to_wstring(static_cast<int>(std::lround(controls.warmth * 100.0))) + L"%"};
  for (std::size_t index = 0; index < labels.size(); ++index) {
    SetWindowTextW(state.mastering_labels[index], labels[index].c_str());
  }
  const auto& stems = controls.stem_mix;
  const std::array<std::pair<const wchar_t*, double>, 4> stem_values = {{
      {L"Drums", stems.drums_db}, {L"Bass stem", stems.bass_db},
      {L"Vocals", stems.vocals_db}, {L"Other", stems.other_db}}};
  for (std::size_t index = 0; index < stem_values.size(); ++index) {
    std::wostringstream label;
    label << stem_values[index].first << L"  " << std::fixed
          << std::setprecision(1) << stem_values[index].second << L" dB";
    SetWindowTextW(state.stem_mix_labels[index], label.str().c_str());
  }
}

void set_mastering_sliders(AppState& state) {
  const auto& controls = state.mastering_controls;
  SendMessageW(state.mastering_sliders[0], TBM_SETPOS, TRUE,
               static_cast<LPARAM>(std::lround(-controls.target_lufs * 10.0)));
  SendMessageW(state.mastering_sliders[1], TBM_SETPOS, TRUE,
               static_cast<LPARAM>(std::lround(controls.bass_db * 10.0 + 30.0)));
  SendMessageW(state.mastering_sliders[2], TBM_SETPOS, TRUE,
               static_cast<LPARAM>(std::lround(controls.presence_db * 10.0 + 30.0)));
  SendMessageW(state.mastering_sliders[3], TBM_SETPOS, TRUE,
               static_cast<LPARAM>(std::lround(controls.width * 100.0)));
  SendMessageW(state.mastering_sliders[4], TBM_SETPOS, TRUE,
               static_cast<LPARAM>(std::lround(controls.punch * 100.0)));
  SendMessageW(state.mastering_sliders[5], TBM_SETPOS, TRUE,
               static_cast<LPARAM>(std::lround(controls.warmth * 100.0)));
  const std::array<double, 4> stem_gains = {
      controls.stem_mix.drums_db, controls.stem_mix.bass_db,
      controls.stem_mix.vocals_db, controls.stem_mix.other_db};
  for (std::size_t index = 0; index < stem_gains.size(); ++index) {
    SendMessageW(state.stem_mix_sliders[index], TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::lround(stem_gains[index] * 2.0 + 24.0)));
  }
  update_mastering_control_labels(state);
}

void read_mastering_sliders(AppState& state) {
  state.mastering_controls.target_lufs =
      -static_cast<double>(SendMessageW(state.mastering_sliders[0], TBM_GETPOS, 0, 0)) / 10.0;
  state.mastering_controls.bass_db =
      (static_cast<double>(SendMessageW(state.mastering_sliders[1], TBM_GETPOS, 0, 0)) - 30.0) / 10.0;
  state.mastering_controls.presence_db =
      (static_cast<double>(SendMessageW(state.mastering_sliders[2], TBM_GETPOS, 0, 0)) - 30.0) / 10.0;
  state.mastering_controls.width =
      static_cast<double>(SendMessageW(state.mastering_sliders[3], TBM_GETPOS, 0, 0)) / 100.0;
  state.mastering_controls.punch =
      static_cast<double>(SendMessageW(state.mastering_sliders[4], TBM_GETPOS, 0, 0)) / 100.0;
  state.mastering_controls.warmth =
      static_cast<double>(SendMessageW(state.mastering_sliders[5], TBM_GETPOS, 0, 0)) / 100.0;
  state.mastering_controls.stem_mix.drums_db =
      (static_cast<double>(SendMessageW(state.stem_mix_sliders[0], TBM_GETPOS, 0, 0)) - 24.0) / 2.0;
  state.mastering_controls.stem_mix.bass_db =
      (static_cast<double>(SendMessageW(state.stem_mix_sliders[1], TBM_GETPOS, 0, 0)) - 24.0) / 2.0;
  state.mastering_controls.stem_mix.vocals_db =
      (static_cast<double>(SendMessageW(state.stem_mix_sliders[2], TBM_GETPOS, 0, 0)) - 24.0) / 2.0;
  state.mastering_controls.stem_mix.other_db =
      (static_cast<double>(SendMessageW(state.stem_mix_sliders[3], TBM_GETPOS, 0, 0)) - 24.0) / 2.0;
  update_mastering_control_labels(state);
}

void select_mastering_style(AppState& state) {
  const int selected = static_cast<int>(
      SendMessageW(state.style_combo, CB_GETCURSEL, 0, 0));
  if (selected < 0 || selected > 5) return;
  const auto stem_mix = state.mastering_controls.stem_mix;
  state.mastering_controls = amt::mastering::mastering_style_preset(
      static_cast<amt::mastering::MasteringStyle>(selected));
  state.mastering_controls.stem_mix = stem_mix;
  set_mastering_sliders(state);
}

void update_controls(AppState& state) {
  const bool has_source = state.metadata.has_value();
  const bool busy = state.busy.load(std::memory_order_acquire);
  bool has_analysis = false;
  bool has_masters = false;
  {
    std::scoped_lock lock(state.data_mutex);
    has_analysis = state.analysis.has_value();
    has_masters = state.project && state.project->master_a.available && state.project->master_b.available;
  }
  EnableWindow(state.open_button, !busy);
  EnableWindow(state.recent_button, !busy);
  EnableWindow(state.analyze_button, has_source && !busy);
  EnableWindow(state.master_button, has_analysis && !busy);
  EnableWindow(state.export_button, has_source && !busy);
  EnableWindow(state.cancel_button, busy);
  EnableWindow(state.recipe_combo, !busy);
  EnableWindow(state.settings_button, !busy);
  EnableWindow(state.batch_button, !busy);
  EnableWindow(state.play_button, has_source && state.playable);
  EnableWindow(state.stop_button, has_source && state.playable);
  EnableWindow(state.seek, has_source && state.playable);
  EnableWindow(state.original_button, has_masters && state.comparison_ready && state.playable);
  EnableWindow(state.master_a_button, has_masters && state.comparison_ready && state.playable);
  EnableWindow(state.master_b_button, has_masters && state.comparison_ready && state.playable);
  EnableWindow(state.translation_combo, has_masters && !busy);
  EnableWindow(state.revision_edit, has_masters && !busy);
  EnableWindow(state.revision_button, has_masters && !busy);
  EnableWindow(state.style_combo, !busy);
  for (HWND slider : state.mastering_sliders) EnableWindow(slider, !busy);
  for (HWND slider : state.stem_mix_sliders) EnableWindow(slider, !busy);
}

void update_details(AppState& state) {
  std::scoped_lock lock(state.data_mutex);
  std::wostringstream text;
  text << std::fixed << std::setprecision(2);
  if (!state.project) {
    text << L"Drop a WAV, AIFF, or FLAC file here, or choose Open.\r\n"
         << L"The normal workflow is Analyze → Master → loudness-matched Original/A/B → Natural Revision → Export.";
    SetWindowTextW(state.details, text.str().c_str());
    return;
  }

  const auto& project = *state.project;
  text << L"PROJECT\r\n"
       << widen_utf8(project.display_name) << L"\r\n"
       << L"History nodes: " << project.revisions.size() << L"\r\n"
       << L"Selected: " << selection_label(project.selected) << L"\r\n"
       << L"Master controls: " << state.mastering_controls.target_lufs
       << L" LUFS, bass " << state.mastering_controls.bass_db
       << L" dB, presence " << state.mastering_controls.presence_db
       << L" dB, width " << state.mastering_controls.width * 100.0
       << L"%, punch " << state.mastering_controls.punch * 100.0
       << L"%, warmth " << state.mastering_controls.warmth * 100.0 << L"%\r\n"
       << L"Stem balance: drums " << state.mastering_controls.stem_mix.drums_db
       << L" dB, bass " << state.mastering_controls.stem_mix.bass_db
       << L" dB, vocals " << state.mastering_controls.stem_mix.vocals_db
       << L" dB, other " << state.mastering_controls.stem_mix.other_db << L" dB\r\n";

  if (!state.analysis) {
    if (project.source_integrated_lufs > -79.0) {
      text << L"Saved source loudness: " << project.source_integrated_lufs << L" LUFS\r\n";
    }
    if (project.master_a.available) {
      text << L"Master A: " << project.master_a.integrated_lufs << L" LUFS / "
           << project.master_a.true_peak_dbtp << L" dBTP — Recommended\r\n";
    }
    if (project.master_b.available) {
      text << L"Master B: " << project.master_b.integrated_lufs << L" LUFS / "
           << project.master_b.true_peak_dbtp << L" dBTP — Preservation Alternative\r\n";
    }
    text << L"\r\nAnalyze to refresh waveform, Mix Health, sections, and findings.";
  } else {
    const auto& report = *state.analysis;
    const auto& technical = report.technical;
    text << L"\r\nSOURCE\r\n"
         << L"Integrated: " << technical.loudness.integrated_lufs << L" LUFS\r\n"
         << L"True peak: " << technical.loudness.true_peak_dbtp << L" dBTP\r\n"
         << L"PLR: " << technical.loudness.peak_to_loudness_ratio_db << L" dB\r\n"
         << L"Crest: " << technical.loudness.crest_factor_db << L" dB\r\n"
         << L"Tempo estimate: " << report.structural.tempo.bpm << L" BPM (confidence "
         << report.structural.tempo.confidence << L")\r\n"
         << L"Sections: " << report.structural.sections.size() << L" — contrast "
         << report.structural.macro_dynamics.section_contrast_db << L" dB\r\n"
         << L"\r\nMIX HEALTH V1 — heuristic, not an objective quality score\r\n"
         << L"Overall: " << report.mix_health.overall_heuristic_score << L" / 100 — "
         << widen_utf8(report.mix_health.overall_assessment) << L"\r\n";
    for (const auto& dimension : report.mix_health.dimensions) {
      text << L"  " << widen_utf8(dimension.label) << L": " << dimension.heuristic_score
           << L" — " << widen_utf8(dimension.assessment) << L"\r\n";
    }
    text << L"\r\nFINDINGS\r\n";
    for (std::size_t index = 0U; index < std::min<std::size_t>(10U, report.findings.size()); ++index) {
      const auto& finding = report.findings[index];
      text << L"[" << widen_utf8(amt::analysis::finding_severity_name(finding.severity)) << L"] "
           << widen_utf8(finding.title) << L"\r\n  " << widen_utf8(finding.detail) << L"\r\n";
    }
  }

  if (state.plan) {
    text << L"\r\nMASTERING DECISION\r\n"
         << L"Master A — Recommended — target " << state.plan->master_a.target_lufs << L" LUFS\r\n";
    for (const auto& reason : state.plan->master_a.rationale) {
      text << L"  • " << widen_utf8(reason) << L"\r\n";
    }
    text << L"Master B — Preservation Alternative — target " << state.plan->master_b.target_lufs
         << L" LUFS\r\n";
  }

  if (!state.last_revision_explanation.empty()) {
    text << L"\r\nLAST NATURAL-LANGUAGE REVISION\r\n"
         << widen_utf8(state.last_revision_explanation) << L"\r\n";
  }

  const auto* current_class = amt::translation::find_playback_class(state.active_translation);
  if (current_class) {
    text << L"\r\nPLAYBACK TRANSLATION SIMULATION (" << widen_utf8(current_class->name) << L")\r\n"
         << L"Low Cutoff: " << current_class->low_cutoff_hz << L" Hz — High Cutoff: "
         << current_class->high_cutoff_hz << L" Hz\r\n"
         << L"Mono Fold: " << (current_class->fold_to_mono ? L"Yes (Mono Summed)" : L"No (Stereo Kept)")
         << L" — Max Linear Peak: " << current_class->max_linear_peak_db << L" dBTP\r\n";
  }

  if (project.source_diagnostics) {
    const auto& diagnostics = *project.source_diagnostics;
    text << L"\r\nSOURCE DIAGNOSTICS (restored)\r\n"
         << widen_utf8(diagnostics.summary) << L"\r\n"
         << L"Source-guided stereo processing: "
         << (diagnostics.source_guidance_applied ? L"applied" : L"not applied") << L"\r\n"
         << L"Automatic Mode 1: "
         << (diagnostics.automatic_mode1_approved ? L"approved" : L"calibration-gated")
         << L"\r\n";
  }

  text << L"\r\nRECENT HISTORY\r\n";
  const std::size_t first = project.revisions.size() > 8U ? project.revisions.size() - 8U : 0U;
  for (std::size_t index = first; index < project.revisions.size(); ++index) {
    const auto& revision = project.revisions[index];
    text << L"  " << widen_utf8(revision.kind) << L": " << widen_utf8(revision.summary) << L"\r\n";
  }
  SetWindowTextW(state.details, text.str().c_str());
}

RECT waveform_rect(HWND window) {
  RECT client{};
  GetClientRect(window, &client);
  const int height = static_cast<int>(client.bottom - client.top);
  const int waveform_height = std::max(100, std::min(200, height / 3));
  return RECT{12, 262, std::max<LONG>(13, client.right - 12),
              static_cast<LONG>(262 + waveform_height)};
}

void draw_waveform(AppState& state, HDC dc) {
  const RECT area = waveform_rect(state.window);
  HBRUSH background = CreateSolidBrush(RGB(24, 27, 32));
  FillRect(dc, &area, background);
  DeleteObject(background);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(205, 211, 220));

  std::scoped_lock lock(state.data_mutex);
  if (!state.analysis || state.analysis->technical.waveform.levels.empty()) {
    RECT label = area;
    DrawTextW(dc, L"Waveform appears after Analyze.", -1, &label,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }

  const auto& waveform = state.analysis->technical.waveform;
  const int width = std::max(1, static_cast<int>(area.right - area.left));
  const amt::audio::WaveformLevel* level = &waveform.levels.back();
  for (const auto& candidate : waveform.levels) {
    if (!candidate.channels.empty() &&
        candidate.channels.front().size() <= static_cast<std::size_t>(width * 2)) {
      level = &candidate;
      break;
    }
  }
  if (level->channels.empty() || level->channels.front().empty()) return;

  HPEN center_pen = CreatePen(PS_SOLID, 1, RGB(65, 70, 80));
  HPEN wave_pen = CreatePen(PS_SOLID, 1, RGB(71, 197, 214));
  HPEN previous = static_cast<HPEN>(SelectObject(dc, center_pen));
  const std::size_t channel_count = std::min<std::size_t>(2U, level->channels.size());
  const int total_height = static_cast<int>(area.bottom - area.top);
  for (std::size_t channel = 0U; channel < channel_count; ++channel) {
    const int top = static_cast<int>(area.top) +
        static_cast<int>((static_cast<std::int64_t>(total_height) * channel) / channel_count);
    const int bottom = static_cast<int>(area.top) +
        static_cast<int>((static_cast<std::int64_t>(total_height) * (channel + 1U)) / channel_count);
    const int center = (top + bottom) / 2;
    MoveToEx(dc, area.left, center, nullptr);
    LineTo(dc, area.right, center);
    SelectObject(dc, wave_pen);
    const auto& bins = level->channels[channel];
    const double scale = static_cast<double>(bottom - top) * 0.45;
    for (int pixel = 0; pixel < width; ++pixel) {
      const auto index = std::min<std::size_t>(bins.size() - 1U,
          static_cast<std::size_t>((static_cast<std::uint64_t>(pixel) * bins.size()) /
                                   static_cast<std::uint64_t>(width)));
      const auto& bin = bins[index];
      const int y1 = center - static_cast<int>(std::lround(
          std::clamp(static_cast<double>(bin.maximum), -1.2, 1.2) * scale));
      const int y2 = center - static_cast<int>(std::lround(
          std::clamp(static_cast<double>(bin.minimum), -1.2, 1.2) * scale));
      MoveToEx(dc, static_cast<int>(area.left) + pixel, y1, nullptr);
      LineTo(dc, static_cast<int>(area.left) + pixel, y2 + 1);
    }
    SelectObject(dc, center_pen);
  }
  SelectObject(dc, previous);
  DeleteObject(center_pen);
  DeleteObject(wave_pen);

  if (state.metadata && state.metadata->frames > 0) {
    const auto frame = std::clamp<std::int64_t>(playhead_frame(state), 0, state.metadata->frames);
    const int x = static_cast<int>(area.left) + static_cast<int>(
        (static_cast<long double>(frame) * width) /
        static_cast<long double>(state.metadata->frames));
    HPEN playhead_pen = CreatePen(PS_SOLID, 1, RGB(245, 245, 245));
    previous = static_cast<HPEN>(SelectObject(dc, playhead_pen));
    MoveToEx(dc, x, area.top, nullptr);
    LineTo(dc, x, area.bottom);
    SelectObject(dc, previous);
    DeleteObject(playhead_pen);
  }
}

void layout(AppState& state) {
  RECT client{};
  GetClientRect(state.window, &client);
  const int width = static_cast<int>(client.right - client.left);
  const int height = static_cast<int>(client.bottom - client.top);
  constexpr int margin = 12;
  constexpr int gap = 6;
  constexpr int button_height = 28;
  constexpr int button_width = 80;

  // Row 1: Action bar
  int x = margin;
  for (HWND button : {state.open_button, state.recent_button, state.analyze_button,
                      state.master_button, state.export_button, state.cancel_button,
                      state.batch_button, state.settings_button}) {
    const int w = (button == state.batch_button) ? 92 : button_width;
    MoveWindow(button, x, margin, w, button_height, TRUE);
    x += w + gap;
  }
  MoveWindow(state.recipe_combo, x, margin, std::max(160, width - x - margin), 300, TRUE);

  // Row 2: Transport and Auditioning
  x = margin;
  for (HWND button : {state.play_button, state.stop_button, state.original_button,
                      state.master_a_button, state.master_b_button}) {
    const int current_width = button == state.master_a_button ? 140 : 88;
    MoveWindow(button, x, 46, current_width, button_height, TRUE);
    x += current_width + gap;
  }
  MoveWindow(state.translation_combo, x, 46, std::max(180, width - x - margin), 300, TRUE);

  // Row 3: Natural Language Revision Bar
  const int rev_button_width = 88;
  const int rev_edit_width = std::max(100, width - margin * 2 - rev_button_width - gap);
  MoveWindow(state.revision_edit, margin, 80, rev_edit_width, button_height, TRUE);
  MoveWindow(state.revision_button, margin + rev_edit_width + gap, 80, rev_button_width, button_height, TRUE);

  // Row 4: hands-on mastering controls.
  constexpr int mastering_top = 114;
  constexpr int mastering_label_height = 18;
  constexpr int mastering_control_height = 30;
  constexpr int mastering_columns = 7;
  const int mastering_width = std::max(70, (width - margin * 2 - gap * (mastering_columns - 1)) /
                                               mastering_columns);
  x = margin;
  MoveWindow(state.style_label, x, mastering_top, mastering_width,
             mastering_label_height, TRUE);
  MoveWindow(state.style_combo, x, mastering_top + mastering_label_height,
             mastering_width, 180, TRUE);
  x += mastering_width + gap;
  for (std::size_t index = 0; index < state.mastering_sliders.size(); ++index) {
    MoveWindow(state.mastering_labels[index], x, mastering_top, mastering_width,
               mastering_label_height, TRUE);
    MoveWindow(state.mastering_sliders[index], x,
               mastering_top + mastering_label_height, mastering_width,
               mastering_control_height, TRUE);
    x += mastering_width + gap;
  }

  // Row 5: actual separated-source balance controls. Zero is a transparent
  // pass-through; non-zero values trigger cached HTDemucs separation.
  constexpr int stem_top = 166;
  constexpr int stem_columns = 4;
  const int stem_width = std::max(
      100, (width - margin * 2 - gap * (stem_columns - 1)) / stem_columns);
  x = margin;
  for (std::size_t index = 0; index < state.stem_mix_sliders.size(); ++index) {
    MoveWindow(state.stem_mix_labels[index], x, stem_top, stem_width,
               mastering_label_height, TRUE);
    MoveWindow(state.stem_mix_sliders[index], x,
               stem_top + mastering_label_height, stem_width,
               mastering_control_height, TRUE);
    x += stem_width + gap;
  }

  // Status message
  MoveWindow(state.status, margin, 236, std::max(10, width - 2 * margin), 22, TRUE);

  // Waveform, Seek, Progress, Details
  const RECT wave = waveform_rect(state.window);
  MoveWindow(state.seek, margin, wave.bottom + 4, std::max(10, width - 2 * margin), 28, TRUE);
  MoveWindow(state.progress, margin, wave.bottom + 34, std::max(10, width - 2 * margin), 12, TRUE);
  const int details_top = static_cast<int>(wave.bottom) + 52;
  MoveWindow(state.details, margin, details_top, std::max(10, width - 2 * margin),
             std::max(60, height - details_top - margin), TRUE);
}

void populate_recipes(AppState& state) {
  SendMessageW(state.recipe_combo, CB_RESETCONTENT, 0, 0);
  const auto& recipes = amt::project::builtin_export_recipes();
  for (const auto& recipe : recipes) {
    std::wstring label = widen_utf8(recipe.name);
    if (!recipe.available) label += L" — unavailable";
    SendMessageW(state.recipe_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
  }
  SendMessageW(state.recipe_combo, CB_SETCURSEL, 0, 0);
}

void populate_translations(AppState& state) {
  SendMessageW(state.translation_combo, CB_RESETCONTENT, 0, 0);
  for (const auto& item : amt::translation::builtin_playback_classes()) {
    std::wstring label = L"Preview: " + widen_utf8(item.name);
    SendMessageW(state.translation_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
  }
  SendMessageW(state.translation_combo, CB_SETCURSEL, 0, 0);
}

const amt::project::ExportRecipe* selected_recipe(AppState& state) {
  const auto selection = static_cast<int>(SendMessageW(state.recipe_combo, CB_GETCURSEL, 0, 0));
  const auto& recipes = amt::project::builtin_export_recipes();
  if (selection < 0 || static_cast<std::size_t>(selection) >= recipes.size()) return nullptr;
  return &recipes[static_cast<std::size_t>(selection)];
}

bool prepare_comparison_from_project(AppState& state) {
  amt::project::ProjectRecord project;
  {
    std::scoped_lock lock(state.data_mutex);
    if (!state.project) return false;
    project = *state.project;
  }
  if (!state.playable || !project.master_a.available || !project.master_b.available ||
      !std::filesystem::exists(project.master_a.path) || !std::filesystem::exists(project.master_b.path)) {
    state.comparison_ready = false;
    return false;
  }

  amt::analysis::LoudnessMetrics original_loudness;
  amt::analysis::LoudnessMetrics a_loudness;
  amt::analysis::LoudnessMetrics b_loudness;
  original_loudness.integrated_lufs = project.source_integrated_lufs;
  a_loudness.integrated_lufs = project.master_a.integrated_lufs;
  b_loudness.integrated_lufs = project.master_b.integrated_lufs;
  const auto match = amt::mastering::make_loudness_match_profile(
      original_loudness, a_loudness, b_loudness);

  state.transport.stop();
  std::string error;
  if (!state.comparison.load(
          {.path = project.source_path, .audition_gain_db = match.original_gain_db},
          {.path = project.master_a.path, .audition_gain_db = match.master_a_gain_db},
          {.path = project.master_b.path, .audition_gain_db = match.master_b_gain_db}, error)) {
    state.comparison_ready = false;
    return false;
  }
  state.comparison_ready = true;
  state.comparison.select(comparison_source(project.selected));
  return true;
}

bool initialize_source(AppState& state, const std::filesystem::path& source,
                       std::optional<amt::project::ProjectRecord> existing_project) {
  std::string error;
  const auto metadata = state.codecs.probe(source, error);
  if (!metadata) {
    show_error(state.window, L"Unable to open audio", error);
    return false;
  }

  stop_playback(state);
  state.comparison_ready = false;
  state.playable = metadata->channels >= 1 && metadata->channels <= 2;
  if (state.playable && !state.transport.load(source, error)) {
    show_error(state.window, L"Unable to initialize playback", error);
    return false;
  }

  if (!existing_project) {
    auto created = state.projects.create(source, error);
    if (created.project_id.empty()) {
      show_error(state.window, L"Unable to create project", error);
      return false;
    }
    existing_project = std::move(created);
  }

  {
    std::scoped_lock lock(state.data_mutex);
    state.source_path = source;
    state.metadata = *metadata;
    state.project = std::move(*existing_project);
    state.analysis.reset();
    state.plan.reset();
    state.rendered.reset();
    state.worker_error.clear();
  }

  prepare_comparison_from_project(state);
  state.last_seek_position = 0;
  SendMessageW(state.seek, TBM_SETPOS, TRUE, 0);
  SendMessageW(state.progress, PBM_SETPOS, 0, 0);
  update_controls(state);
  update_play_button(state);
  update_details(state);
  std::wostringstream status;
  status << L"Project: " << widen_utf8(state.project->display_name) << L" — "
         << metadata->sample_rate << L" Hz / " << metadata->channels << L" ch";
  if (state.comparison_ready) status << L" — saved Original/A/B restored";
  set_status(state, status.str());
  InvalidateRect(state.window, nullptr, FALSE);
  return true;
}

std::optional<amt::project::ProjectRecord> find_project_for_source(
    AppState& state, const std::filesystem::path& source) {
  std::string error;
  const auto recent = state.projects.list_recent(100U, error);
  const auto normalized = source.lexically_normal();
  const auto iterator = std::find_if(recent.begin(), recent.end(), [&](const auto& project) {
    return project.source_path.lexically_normal() == normalized;
  });
  return iterator == recent.end() ? std::nullopt : std::optional<amt::project::ProjectRecord>(*iterator);
}

void open_source_path(AppState& state, const std::filesystem::path& source) {
  if (state.busy.load(std::memory_order_acquire)) return;
  initialize_source(state, source, find_project_for_source(state, source));
}

void choose_source(AppState& state) {
  wchar_t buffer[32768]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = state.window;
  dialog.lpstrFilter = L"Supported audio (*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.aac)\0*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.aac\0Lossless audio (*.wav;*.aif;*.flac)\0*.wav;*.wave;*.aif;*.aiff;*.flac\0All files (*.*)\0*.*\0\0";
  dialog.lpstrFile = buffer;
  dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetOpenFileNameW(&dialog) != FALSE) open_source_path(state, buffer);

}

void choose_recent(AppState& state) {
  std::string error;
  const auto recent = state.projects.list_recent(12U, error);
  if (!error.empty()) {
    show_error(state.window, L"Recent projects", error);
    return;
  }
  if (recent.empty()) {
    MessageBoxW(state.window, L"No saved projects yet.", L"Recent projects", MB_OK | MB_ICONINFORMATION);
    return;
  }

  HMENU menu = CreatePopupMenu();
  for (std::size_t index = 0U; index < recent.size(); ++index) {
    std::wstring label = widen_utf8(recent[index].display_name);
    label += L" — ";
    label += selection_label(recent[index].selected);
    AppendMenuW(menu, MF_STRING, kRecentMenuBase + static_cast<UINT>(index), label.c_str());
  }
  RECT button{};
  GetWindowRect(state.recent_button, &button);
  const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                      button.left, button.bottom, 0, state.window, nullptr);
  DestroyMenu(menu);
  if (command < kRecentMenuBase) return;
  const std::size_t index = static_cast<std::size_t>(command - kRecentMenuBase);
  if (index >= recent.size()) return;
  if (!std::filesystem::exists(recent[index].source_path)) {
    show_error(state.window, L"Project source missing",
               "The saved source file no longer exists at: " + path_utf8(recent[index].source_path));
    return;
  }
  initialize_source(state, recent[index].source_path, recent[index]);
}

void start_job(AppState& state, const wchar_t* text) {
  join_worker_if_done(state);
  state.busy.store(true, std::memory_order_release);
  state.cancellation = std::make_shared<amt::core::CancellationToken>();
  {
    std::scoped_lock lock(state.data_mutex);
    state.worker_error.clear();
  }
  SendMessageW(state.progress, PBM_SETPOS, 0, 0);
  set_status(state, text);
  update_controls(state);
}

void begin_analysis(AppState& state) {
  if (!state.metadata || state.busy.load(std::memory_order_acquire)) return;
  stop_playback(state);
  state.comparison_ready = false;
  std::string ignored;
  if (state.playable) state.transport.load(state.source_path, ignored);

  amt::project::ProjectRecord project_snapshot;
  {
    std::scoped_lock lock(state.data_mutex);
    if (!state.project) return;
    project_snapshot = *state.project;
  }
  start_job(state, L"Analyzing structure, dynamics, character, and translation…");
  const auto source = state.source_path;
  const auto cancellation = state.cancellation;
  HWND window = state.window;
  AppState* state_pointer = &state;
  state.worker = std::thread([source, project_snapshot = std::move(project_snapshot), cancellation,
                              window, state_pointer]() mutable {
    amt::codec::SndFileCodecService codecs;
    std::string error;
    auto report = amt::analysis::analyze_track(
        codecs, source, error, cancellation.get(), [window](const double value) {
          PostMessageW(window, kJobProgress,
                       static_cast<WPARAM>(std::clamp(
                           static_cast<int>(std::lround(value * 1000.0)), 0, 1000)), 0);
        });
    if (report) {
      project_snapshot.source_integrated_lufs = report->technical.loudness.integrated_lufs;
      project_snapshot.analysis_json = amt::analysis::analysis_report_to_json(*report);
      std::string store_error;
      if (!state_pointer->projects.append_revision(
              project_snapshot, "analysis", "Structural/perceptual analysis refreshed.", {}, store_error)) {
        error = store_error;
        report.reset();
      }
    }
    {
      std::scoped_lock lock(state_pointer->data_mutex);
      if (report) {
        state_pointer->analysis = std::move(*report);
        state_pointer->project = std::move(project_snapshot);
        state_pointer->plan.reset();
        state_pointer->rendered.reset();
      }
      state_pointer->worker_error = std::move(error);
    }
    state_pointer->busy.store(false, std::memory_order_release);
    PostMessageW(window, kAnalysisFinished, report.has_value() ? 1U : 0U, 0);
  });
}

void begin_mastering(AppState& state) {
  if (state.busy.load(std::memory_order_acquire)) return;
  amt::analysis::AnalysisReport report;
  amt::project::ProjectRecord project_snapshot;
  {
    std::scoped_lock lock(state.data_mutex);
    if (!state.analysis || !state.project) return;
    report = *state.analysis;
    project_snapshot = *state.project;
  }
  stop_playback(state);
  state.comparison_ready = false;
  start_job(state, L"Creating Recommended Master A and preservation-biased Master B…");

  const auto source = state.source_path;
  const auto output_directory = state.projects.root() / project_snapshot.project_id / "renders";
  const auto cancellation = state.cancellation;
  const auto mastering_controls = state.mastering_controls;
  HWND window = state.window;
  AppState* state_pointer = &state;
  state.worker = std::thread([source, output_directory, report = std::move(report),
                              project_snapshot = std::move(project_snapshot),
                              mastering_controls, cancellation,
                              window, state_pointer]() mutable {
    amt::codec::SndFileCodecService codecs;
    std::string error;
    auto plan = amt::mastering::plan_mastering(report);
    amt::mastering::apply_mastering_controls(plan, mastering_controls);
    auto rendered = amt::mastering::render_mastering_plan(
        codecs, source, output_directory, report.technical, plan, error, {}, cancellation.get(),
        [window](const double value) {
          PostMessageW(window, kJobProgress,
                       static_cast<WPARAM>(std::clamp(
                           static_cast<int>(std::lround(value * 1000.0)), 0, 1000)), 0);
        });
    if (rendered) {
      project_snapshot.master_a = {
          .available = true,
          .path = rendered->master_a.output_path,
          .integrated_lufs = rendered->master_a.analysis.loudness.integrated_lufs,
          .true_peak_dbtp = rendered->master_a.analysis.loudness.true_peak_dbtp,
          .recommended = true};
      project_snapshot.master_b = {
          .available = true,
          .path = rendered->master_b.output_path,
          .integrated_lufs = rendered->master_b.analysis.loudness.integrated_lufs,
          .true_peak_dbtp = rendered->master_b.analysis.loudness.true_peak_dbtp,
          .recommended = false};
      project_snapshot.master_a_graph_json = plan.master_a.graph.to_json();
      project_snapshot.master_b_graph_json = plan.master_b.graph.to_json();
      project_snapshot.selected = amt::project::CandidateSelection::master_a;
      std::string store_error;
      if (!state_pointer->projects.append_revision(
              project_snapshot, "mastering",
              "Rendered Master A (recommended) and Master B (preservation alternative).",
              rendered->master_a.output_path, store_error)) {
        error = store_error;
        rendered.reset();
      }
    }
    {
      std::scoped_lock lock(state_pointer->data_mutex);
      if (rendered) {
        state_pointer->plan = std::move(plan);
        state_pointer->rendered = std::move(*rendered);
        state_pointer->project = std::move(project_snapshot);
      }
      state_pointer->worker_error = std::move(error);
    }
    state_pointer->busy.store(false, std::memory_order_release);
    PostMessageW(window, kMasterFinished, rendered.has_value() ? 1U : 0U, 0);
  });
}

struct ExportSelection {
  std::filesystem::path path;
  amt::project::CandidateSelection selection{amt::project::CandidateSelection::original};
};

ExportSelection current_export_source(AppState& state) {
  std::scoped_lock lock(state.data_mutex);
  if (!state.project) return {.path = state.source_path};
  const auto selected = state.project->selected;
  if (selected == amt::project::CandidateSelection::master_a && state.project->master_a.available) {
    return {.path = state.project->master_a.path, .selection = selected};
  }
  if (selected == amt::project::CandidateSelection::master_b && state.project->master_b.available) {
    return {.path = state.project->master_b.path, .selection = selected};
  }
  return {.path = state.source_path, .selection = amt::project::CandidateSelection::original};
}

std::optional<std::filesystem::path> choose_export_path(
    AppState& state, const ExportSelection& selection,
    const amt::project::ExportRecipe& recipe) {
  wchar_t buffer[32768]{};
  std::wstring suggested = state.source_path.stem().wstring() + L"_" +
                           selection_label(selection.selection) + L"_" + widen_utf8(recipe.key);
  const auto count = std::min<std::size_t>(suggested.size(), std::size(buffer) - 1U);
  std::copy_n(suggested.c_str(), count, buffer);
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = state.window;
  dialog.lpstrFilter = L"Audio export\0*.*\0\0";
  dialog.lpstrFile = buffer;
  dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetSaveFileNameW(&dialog) == FALSE) return std::nullopt;
  std::filesystem::path output(buffer);
  output.replace_extension(widen_utf8(recipe.extension));
  return output;
}

void begin_export(AppState& state) {
  if (!state.metadata || state.busy.load(std::memory_order_acquire)) return;
  const auto* recipe = selected_recipe(state);
  if (recipe == nullptr) return;
  if (!recipe->available) {
    show_error(state.window, L"Export recipe unavailable", recipe->unavailable_reason);
    return;
  }
  const auto selection = current_export_source(state);
  if (selection.path.empty() || !std::filesystem::exists(selection.path)) {
    show_error(state.window, L"Export source missing", "The selected audio version is not available on disk.");
    return;
  }
  const auto output = choose_export_path(state, selection, *recipe);
  if (!output) return;
  if (output->lexically_normal() == selection.path.lexically_normal()) {
    show_error(state.window, L"Export path", "Choose a different path so the selected version is not overwritten in place.");
    return;
  }

  amt::project::ProjectRecord project_snapshot;
  {
    std::scoped_lock lock(state.data_mutex);
    if (!state.project) return;
    project_snapshot = *state.project;
  }
  const auto request = amt::project::make_export_request(*recipe);
  const std::string recipe_name = recipe->name;
  const std::string selection_name = amt::project::selection_name(selection.selection);
  start_job(state, L"Exporting selected version…");
  const auto cancellation = state.cancellation;
  HWND window = state.window;
  AppState* state_pointer = &state;
  state.worker = std::thread([source = selection.path, output = *output, request,
                              recipe_name, selection_name,
                              project_snapshot = std::move(project_snapshot), cancellation,
                              window, state_pointer]() mutable {
    amt::codec::SndFileCodecService codecs;
    std::string error;
    bool success = amt::codec::export_audio(
        codecs, source, output, request, error, cancellation.get(), [window](const double value) {
          PostMessageW(window, kJobProgress,
                       static_cast<WPARAM>(std::clamp(
                           static_cast<int>(std::lround(value * 1000.0)), 0, 1000)), 0);
        });
    if (success) {
      std::string store_error;
      const std::string summary = "Exported " + selection_name + " using " + recipe_name + ".";
      success = state_pointer->projects.append_revision(
          project_snapshot, "export", summary, output, store_error);
      if (!success) error = store_error;
    }
    {
      std::scoped_lock lock(state_pointer->data_mutex);
      if (success) state_pointer->project = std::move(project_snapshot);
      state_pointer->worker_error = std::move(error);
    }
    state_pointer->busy.store(false, std::memory_order_release);
    PostMessageW(window, kExportFinished, success ? 1U : 0U, 0);
  });
}

void begin_revision(AppState& state) {
  if (state.busy.load(std::memory_order_acquire)) return;
  wchar_t buffer[1024]{};
  GetWindowTextW(state.revision_edit, buffer, static_cast<int>(std::size(buffer)));
  std::string prompt = narrow_utf8(buffer);
  if (prompt.empty()) {
    show_error(state.window, L"Revision Prompt", "Please enter a revision instruction (e.g. 'punchier drums', 'tame high end').");
    return;
  }

  amt::mastering::MasteringPlan current_plan;
  amt::project::ProjectRecord project_snapshot;
  amt::project::CandidateSelection target_selection = amt::project::CandidateSelection::master_a;
  {
    std::scoped_lock lock(state.data_mutex);
    if (!state.plan || !state.project || !state.rendered) {
      show_error(state.window, L"Revision Error", "Mastering must be completed before applying revisions.");
      return;
    }
    current_plan = *state.plan;
    project_snapshot = *state.project;
    target_selection = state.project->selected;
  }

  amt::revision::RevisionParser parser;
  auto intent = parser.parse(prompt);
  if (!intent.parsed_successfully) {
    show_error(state.window, L"Revision Parse Error", intent.parse_error);
    return;
  }

  auto val = amt::revision::ConstraintResolver::validate_constraints(intent);
  if (!val.is_valid && !val.violated_constraints.empty()) {
    show_error(state.window, L"Constraint Violation", val.violated_constraints.front());
    return;
  }

  auto& candidate_plan = (target_selection == amt::project::CandidateSelection::master_b)
                             ? current_plan.master_b
                             : current_plan.master_a;

  auto edit_res = amt::revision::PlanEditor::apply_revision(candidate_plan, intent);
  if (!edit_res.success) {
    show_error(state.window, L"Revision Error", edit_res.error);
    return;
  }

  std::string explanation = amt::revision::RevisionExplanation::generate_explanation(intent, edit_res);
  candidate_plan = edit_res.revised_plan;

  stop_playback(state);
  state.comparison_ready = false;
  start_job(state, L"Applying natural-language revision…");

  const auto source = state.source_path;
  const auto output_dir = state.projects.root() / project_snapshot.project_id / "renders";
  const auto cancellation = state.cancellation;
  HWND window = state.window;
  AppState* state_pointer = &state;

  state.worker = std::thread([source, output_dir, current_plan, explanation,
                              project_snapshot = std::move(project_snapshot),
                              target_selection, cancellation, window, state_pointer]() mutable {
    amt::codec::SndFileCodecService codecs;
    std::string error;
    const auto out_path = output_dir / ((target_selection == amt::project::CandidateSelection::master_b)
                                            ? "master_b_revised.wav"
                                            : "master_a_revised.wav");
    const auto& cand = (target_selection == amt::project::CandidateSelection::master_b)
                           ? current_plan.master_b
                           : current_plan.master_a;

    auto rendered_candidate = amt::mastering::render_candidate(
        codecs, source, out_path, cand, error, {}, cancellation.get(),
        [window](const double value) {
          PostMessageW(window, kJobProgress,
                       static_cast<WPARAM>(std::clamp(
                           static_cast<int>(std::lround(value * 1000.0)), 0, 1000)), 0);
        });

    if (rendered_candidate) {
      if (target_selection == amt::project::CandidateSelection::master_b) {
        project_snapshot.master_b = {
            .available = true,
            .path = rendered_candidate->output_path,
            .integrated_lufs = rendered_candidate->analysis.loudness.integrated_lufs,
            .true_peak_dbtp = rendered_candidate->analysis.loudness.true_peak_dbtp,
            .recommended = false};
      } else {
        project_snapshot.master_a = {
            .available = true,
            .path = rendered_candidate->output_path,
            .integrated_lufs = rendered_candidate->analysis.loudness.integrated_lufs,
            .true_peak_dbtp = rendered_candidate->analysis.loudness.true_peak_dbtp,
            .recommended = true};
      }
      std::string store_err;
      state_pointer->projects.append_revision(
          project_snapshot, "revision", explanation, rendered_candidate->output_path, store_err);
    }

    {
      std::scoped_lock lock(state_pointer->data_mutex);
      if (rendered_candidate) {
        state_pointer->project = std::move(project_snapshot);
        state_pointer->plan = current_plan;
        state_pointer->last_revision_explanation = explanation;
        if (state_pointer->rendered) {
          if (target_selection == amt::project::CandidateSelection::master_b) {
            state_pointer->rendered->master_b = *rendered_candidate;
          } else {
            state_pointer->rendered->master_a = *rendered_candidate;
          }
        }
      }
      state_pointer->worker_error = std::move(error);
    }
    state_pointer->busy.store(false, std::memory_order_release);
    PostMessageW(window, kRevisionFinished, rendered_candidate.has_value() ? 1U : 0U, 0);
  });
}

void show_settings_dialog(AppState& state) {
  amt::settings::SettingsManager settings_mgr;
  std::string err;
  settings_mgr.load(err);
  amt::settings::CacheManager cache_mgr(settings_mgr.settings().cache_directory);
  auto inv = cache_mgr.inspect_cache();

  amt::settings::ModelManager model_mgr(settings_mgr.settings().models_directory);
  auto models = model_mgr.list_installed_models();

  std::wostringstream text;
  text << L"=== SETTINGS & CACHE STATUS ===\r\n\r\n"
       << L"Audio Output: " << widen_utf8(settings_mgr.settings().audio_output_device.empty() ? "Default WASAPI Device" : settings_mgr.settings().audio_output_device) << L"\r\n"
       << L"Buffer Size: " << settings_mgr.settings().buffer_size_frames << L" frames\r\n"
       << L"Cache Directory: " << widen_utf8(settings_mgr.settings().cache_directory.string()) << L"\r\n"
       << L"Cache Items: " << inv.total_items << L" (" << std::fixed << std::setprecision(1) << inv.total_mb << L" MB used of " << settings_mgr.settings().max_cache_size_mb << L" MB budget)\r\n\r\n"
       << L"=== INSTALLED AI MODELS ===\r\n";
  for (const auto& m : models) {
    text << L"• " << widen_utf8(m.name) << L": " << widen_utf8(m.status_description) << L"\r\n";
  }
  text << L"\r\nWould you like to clear temporary disposable cache files now?";

  int res = MessageBoxW(state.window, text.str().c_str(), L"Settings & Cache Management", MB_YESNO | MB_ICONINFORMATION);
  if (res == IDYES) {
    std::string clear_err;
    cache_mgr.clear_all_disposable(clear_err);
    MessageBoxW(state.window, L"Temporary cache files cleared successfully.", L"Cache Cleared", MB_OK | MB_ICONINFORMATION);
  }
}

void begin_batch_dialog(AppState& state) {
  wchar_t buffer[65536]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = state.window;
  dialog.lpstrFilter = L"Supported audio (*.wav;*.wave;*.aif;*.aiff;*.flac)\0*.wav;*.wave;*.aif;*.aiff;*.flac\0All files (*.*)\0*.*\0\0";
  dialog.lpstrFile = buffer;
  dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;

  if (GetOpenFileNameW(&dialog) == FALSE) return;

  std::vector<std::filesystem::path> files;
  const wchar_t* p = buffer;
  std::filesystem::path dir = p;
  p += wcslen(p) + 1;
  if (*p == L'\0') {
    files.push_back(dir);
  } else {
    while (*p != L'\0') {
      files.push_back(dir / p);
      p += wcslen(p) + 1;
    }
  }

  if (files.empty()) return;

  std::wostringstream text;
  text << L"Selected " << files.size() << L" track(s) for Album Batch Mastering:\r\n\r\n";
  for (std::size_t i = 0; i < std::min<std::size_t>(files.size(), 8U); ++i) {
    text << L"• " << widen_utf8(files[i].filename().string()) << L"\r\n";
  }
  if (files.size() > 8U) text << L"• ... and " << (files.size() - 8U) << L" more.\r\n";
  text << L"\r\nMaster collection and compute dynamic cohesion?";

  int res = MessageBoxW(state.window, text.str().c_str(), L"Album Batch Mastering", MB_YESNO | MB_ICONQUESTION);
  if (res != IDYES) return;

  amt::batch::BatchAlbumProject album;
  album.album_name = "Album Master";
  for (std::size_t i = 0; i < files.size(); ++i) {
    amt::batch::BatchTrackItem track;
    track.track_index = static_cast<int>(i + 1);
    track.title = files[i].stem().string();
    track.source_path = files[i];
    track.target_lufs = -14.0;
    album.tracks.push_back(track);
  }

  std::string batch_err;
  auto codecs = std::make_shared<amt::codec::SndFileCodecService>();
  amt::batch::BatchQueue queue(codecs);
  const auto album_out = state.projects.root() / "AlbumRenders";
  std::filesystem::create_directories(album_out);

  bool ok = queue.process_album(album, album_out, batch_err);
  if (ok) {
    MessageBoxW(state.window, L"Album batch mastering completed successfully! Manifest and reports exported to project folder.", L"Batch Complete", MB_OK | MB_ICONINFORMATION);
  } else {
    show_error(state.window, L"Batch Mastering Failed", batch_err);
  }
}

void set_selection(AppState& state, const amt::project::CandidateSelection selection) {
  if (!state.comparison_ready) return;
  state.comparison.select(comparison_source(selection));
  bool changed = false;
  amt::project::ProjectRecord snapshot;
  {
    std::scoped_lock lock(state.data_mutex);
    if (state.project && state.project->selected != selection) {
      state.project->selected = selection;
      snapshot = *state.project;
      changed = true;
    }
  }
  if (changed) {
    std::string ignored;
    state.projects.save(snapshot, ignored);
  }
  std::string error;
  const auto current = state.comparison.state();
  if (current != amt::playback::TransportState::playing &&
      current != amt::playback::TransportState::paused) {
    if (!state.comparison.play(error)) show_error(state.window, L"Audition error", error);
  }
  set_status(state, L"Loudness-matched audition: " + selection_label(selection));
  update_details(state);
  update_play_button(state);
}

void toggle_playback(AppState& state) {
  if (!state.playable) return;
  std::string error;
  bool ok = true;
  if (state.comparison_ready) {
    if (state.comparison.state() == amt::playback::TransportState::playing) ok = state.comparison.pause(error);
    else if (state.comparison.state() == amt::playback::TransportState::paused) ok = state.comparison.resume(error);
    else ok = state.comparison.play(error);
  } else {
    if (state.transport.state() == amt::playback::TransportState::playing) ok = state.transport.pause(error);
    else if (state.transport.state() == amt::playback::TransportState::paused) ok = state.transport.resume(error);
    else ok = state.transport.play(error);
  }
  if (!ok) show_error(state.window, L"Playback error", error);
  update_play_button(state);
}

void seek_from_ui(AppState& state) {
  if (!state.metadata || state.metadata->frames <= 0 || !state.playable) return;
  const auto position = static_cast<int>(SendMessageW(state.seek, TBM_GETPOS, 0, 0));
  const auto frame = static_cast<std::int64_t>(
      static_cast<long double>(position) * state.metadata->frames / kSeekRange);
  std::string error;
  const bool ok = state.comparison_ready ? state.comparison.seek(frame, error)
                                         : state.transport.seek(frame, error);
  if (!ok) show_error(state.window, L"Seek error", error);
}

void cancel_job(AppState& state) {
  if (state.cancellation) {
    state.cancellation->cancel();
    set_status(state, L"Cancelling…");
  }
}

void finish_generic_job(AppState& state, const bool success, const wchar_t* success_text) {
  join_worker_if_done(state);
  state.cancellation.reset();
  SendMessageW(state.progress, PBM_SETPOS, success ? 1000 : 0, 0);
  std::string error;
  {
    std::scoped_lock lock(state.data_mutex);
    error = state.worker_error;
  }
  if (success) set_status(state, success_text);
  else if (error.find("cancel") != std::string::npos) set_status(state, L"Operation cancelled.");
  else {
    set_status(state, L"Operation failed.");
    show_error(state.window, L"Audio operation failed", error);
  }
  update_controls(state);
  update_details(state);
  InvalidateRect(state.window, nullptr, FALSE);
}

void finish_mastering(AppState& state, const bool success) {
  join_worker_if_done(state);
  state.cancellation.reset();
  SendMessageW(state.progress, PBM_SETPOS, success ? 1000 : 0, 0);
  std::string error;
  {
    std::scoped_lock lock(state.data_mutex);
    error = state.worker_error;
  }
  if (success && prepare_comparison_from_project(state)) {
    set_status(state, L"Masters ready — Master A recommended. Original/A/B audition is loudness-matched.");
  } else if (success) {
    set_status(state, L"Masters rendered, but A/B playback could not be restored.");
  } else if (error.find("cancel") != std::string::npos) {
    set_status(state, L"Mastering cancelled.");
  } else {
    set_status(state, L"Mastering failed.");
    show_error(state.window, L"Mastering failed", error);
  }
  update_controls(state);
  update_details(state);
  update_play_button(state);
  InvalidateRect(state.window, nullptr, FALSE);
}

void update_transport_ui(AppState& state) {
  update_play_button(state);
  if (!state.metadata || state.metadata->frames <= 0 || !state.playable) return;
  const auto frame = std::clamp<std::int64_t>(playhead_frame(state), 0, state.metadata->frames);
  const int position = static_cast<int>(
      static_cast<long double>(frame) * kSeekRange / state.metadata->frames);
  if (position == state.last_seek_position) return;
  state.last_seek_position = position;
  SendMessageW(state.seek, TBM_SETPOS, TRUE, position);
  const RECT waveform = waveform_rect(state.window);
  InvalidateRect(state.window, &waveform, FALSE);
}

void create_controls(AppState& state) {
  constexpr DWORD button = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
  state.open_button = CreateWindowExW(0, L"BUTTON", L"Open", button, 0, 0, 0, 0,
                                      state.window, control_menu(kOpenId), nullptr, nullptr);
  state.recent_button = CreateWindowExW(0, L"BUTTON", L"Recent", button, 0, 0, 0, 0,
                                        state.window, control_menu(kRecentId), nullptr, nullptr);
  state.analyze_button = CreateWindowExW(0, L"BUTTON", L"Analyze", button, 0, 0, 0, 0,
                                         state.window, control_menu(kAnalyzeId), nullptr, nullptr);
  state.master_button = CreateWindowExW(0, L"BUTTON", L"Master", button, 0, 0, 0, 0,
                                        state.window, control_menu(kMasterId), nullptr, nullptr);
  state.export_button = CreateWindowExW(0, L"BUTTON", L"Export", button, 0, 0, 0, 0,
                                        state.window, control_menu(kExportId), nullptr, nullptr);
  state.cancel_button = CreateWindowExW(0, L"BUTTON", L"Cancel", button, 0, 0, 0, 0,
                                        state.window, control_menu(kCancelId), nullptr, nullptr);
  state.batch_button = CreateWindowExW(0, L"BUTTON", L"Album Batch", button, 0, 0, 0, 0,
                                       state.window, control_menu(kBatchButtonId), nullptr, nullptr);
  state.settings_button = CreateWindowExW(0, L"BUTTON", L"Settings", button, 0, 0, 0, 0,
                                          state.window, control_menu(kSettingsButtonId), nullptr, nullptr);
  state.recipe_combo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
      0, 0, 0, 0,
      state.window, control_menu(kRecipeId), nullptr, nullptr);

  state.play_button = CreateWindowExW(0, L"BUTTON", L"Play", button, 0, 0, 0, 0,
                                      state.window, control_menu(kPlayId), nullptr, nullptr);
  state.stop_button = CreateWindowExW(0, L"BUTTON", L"Stop", button, 0, 0, 0, 0,
                                      state.window, control_menu(kStopId), nullptr, nullptr);
  state.original_button = CreateWindowExW(0, L"BUTTON", L"Original", button, 0, 0, 0, 0,
                                          state.window, control_menu(kOriginalId), nullptr, nullptr);
  state.master_a_button = CreateWindowExW(0, L"BUTTON", L"Master A — Recommended", button, 0, 0, 0, 0,
                                          state.window, control_menu(kMasterAId), nullptr, nullptr);
  state.master_b_button = CreateWindowExW(0, L"BUTTON", L"Master B", button, 0, 0, 0, 0,
                                          state.window, control_menu(kMasterBId), nullptr, nullptr);
  state.translation_combo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
      0, 0, 0, 0,
      state.window, control_menu(kTranslationComboId), nullptr, nullptr);

  state.revision_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
      state.window, control_menu(kRevisionEditId), nullptr, nullptr);
  SendMessageW(state.revision_edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Natural language revision: e.g. 'punchier kick', 'tame harsh highs', 'preserve sub-bass'"));

  state.revision_button = CreateWindowExW(0, L"BUTTON", L"Revise", button, 0, 0, 0, 0,
                                          state.window, control_menu(kRevisionButtonId), nullptr, nullptr);

  state.style_label = CreateWindowExW(0, L"STATIC", L"Mastering style",
      WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0,
      state.window, nullptr, nullptr, nullptr);
  state.style_combo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
      0, 0, 0, 0, state.window, control_menu(kStyleComboId), nullptr, nullptr);
  for (const wchar_t* style : {L"Balanced", L"Transparent", L"Punchy",
                               L"Warm", L"Wide", L"Loud", L"Custom"}) {
    SendMessageW(state.style_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(style));
  }
  SendMessageW(state.style_combo, CB_SETCURSEL, 0, 0);

  const std::array<int, 6> slider_ids = {
      kTargetSliderId, kBassSliderId, kPresenceSliderId,
      kWidthSliderId, kPunchSliderId, kWarmthSliderId};
  for (std::size_t index = 0; index < state.mastering_sliders.size(); ++index) {
    state.mastering_labels[index] = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, 0, 0, state.window, nullptr, nullptr, nullptr);
    state.mastering_sliders[index] = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
        0, 0, 0, 0, state.window, control_menu(slider_ids[index]), nullptr, nullptr);
  }
  SendMessageW(state.mastering_sliders[0], TBM_SETRANGE, TRUE, MAKELONG(80, 140));
  SendMessageW(state.mastering_sliders[1], TBM_SETRANGE, TRUE, MAKELONG(0, 60));
  SendMessageW(state.mastering_sliders[2], TBM_SETRANGE, TRUE, MAKELONG(0, 60));
  SendMessageW(state.mastering_sliders[3], TBM_SETRANGE, TRUE, MAKELONG(80, 120));
  SendMessageW(state.mastering_sliders[4], TBM_SETRANGE, TRUE, MAKELONG(0, 100));
  SendMessageW(state.mastering_sliders[5], TBM_SETRANGE, TRUE, MAKELONG(0, 100));

  const std::array<int, 4> stem_slider_ids = {
      kDrumsSliderId, kStemBassSliderId, kVocalsSliderId, kOtherSliderId};
  for (std::size_t index = 0; index < state.stem_mix_sliders.size(); ++index) {
    state.stem_mix_labels[index] = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, 0, 0, state.window, nullptr, nullptr, nullptr);
    state.stem_mix_sliders[index] = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
        0, 0, 0, 0, state.window,
        control_menu(stem_slider_ids[index]), nullptr, nullptr);
    SendMessageW(state.stem_mix_sliders[index], TBM_SETRANGE, TRUE,
                 MAKELONG(0, 36));
  }
  set_mastering_sliders(state);

  state.status = CreateWindowExW(0, L"STATIC",
      L"Drop audio here or choose Open. Projects are saved locally automatically.",
      WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, state.window, nullptr, nullptr, nullptr);
  state.seek = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
      0, 0, 0, 0,
      state.window, control_menu(kSeekId), nullptr, nullptr);
  SendMessageW(state.seek, TBM_SETRANGE, TRUE, MAKELONG(0, kSeekRange));
  state.progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
      0, 0, 0, 0, state.window, nullptr, nullptr, nullptr);
  SendMessageW(state.progress, PBM_SETRANGE32, 0, 1000);
  state.details = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
      L"Drop a track to begin.", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
      ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0,
      state.window, nullptr, nullptr, nullptr);
  populate_recipes(state);
  populate_translations(state);
  update_controls(state);
  layout(state);
}

void handle_drop(AppState& state, HDROP drop) {
  wchar_t path[32768]{};
  if (DragQueryFileW(drop, 0U, path, static_cast<UINT>(std::size(path))) > 0U) {
    open_source_path(state, path);
  }
  DragFinish(drop);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  AppState* state = state_from(window);
  switch (message) {
    case WM_NCCREATE: {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      auto* incoming = static_cast<AppState*>(create->lpCreateParams);
      incoming->window = window;
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(incoming));
      return TRUE;
    }
    case WM_CREATE:
      state = state_from(window);
      if (state) {
        create_controls(*state);
        DragAcceptFiles(window, TRUE);
        SetTimer(window, kUiTimer, 100U, nullptr);
      }
      return 0;
    case WM_DROPFILES:
      if (state) handle_drop(*state, reinterpret_cast<HDROP>(wparam));
      return 0;
    case WM_SIZE:
      if (state) layout(*state);
      return 0;
    case WM_COMMAND:
      if (!state) break;
      if (HIWORD(wparam) == CBN_SELCHANGE && LOWORD(wparam) == kTranslationComboId) {
        int sel = static_cast<int>(SendMessageW(state->translation_combo, CB_GETCURSEL, 0, 0));
        const auto& classes = amt::translation::builtin_playback_classes();
        if (sel >= 0 && static_cast<std::size_t>(sel) < classes.size()) {
          state->active_translation = classes[static_cast<std::size_t>(sel)].id;
          update_details(*state);
        }
        return 0;
      }
      if (HIWORD(wparam) == CBN_SELCHANGE && LOWORD(wparam) == kStyleComboId) {
        select_mastering_style(*state);
        return 0;
      }
      if (HIWORD(wparam) == BN_CLICKED || HIWORD(wparam) == 1U) {
        switch (LOWORD(wparam)) {
          case kOpenId: choose_source(*state); return 0;
          case kRecentId: choose_recent(*state); return 0;
          case kAnalyzeId: begin_analysis(*state); return 0;
          case kMasterId: begin_mastering(*state); return 0;
          case kExportId: begin_export(*state); return 0;
          case kCancelId: cancel_job(*state); return 0;
          case kPlayId: toggle_playback(*state); return 0;
          case kStopId: stop_playback(*state); update_play_button(*state); return 0;
          case kOriginalId: set_selection(*state, amt::project::CandidateSelection::original); return 0;
          case kMasterAId: set_selection(*state, amt::project::CandidateSelection::master_a); return 0;
          case kMasterBId: set_selection(*state, amt::project::CandidateSelection::master_b); return 0;
          case kRevisionButtonId: begin_revision(*state); return 0;
          case kSettingsButtonId: show_settings_dialog(*state); return 0;
          case kBatchButtonId: begin_batch_dialog(*state); return 0;
          default: break;
        }
      }
      break;
    case WM_HSCROLL:
      if (state) {
        const HWND control = reinterpret_cast<HWND>(lparam);
        if (control == state->seek) {
          const auto code = LOWORD(wparam);
          if (code == TB_ENDTRACK || code == TB_THUMBPOSITION) seek_from_ui(*state);
        } else if (std::find(state->mastering_sliders.begin(),
                            state->mastering_sliders.end(), control) !=
                       state->mastering_sliders.end() ||
                   std::find(state->stem_mix_sliders.begin(),
                             state->stem_mix_sliders.end(), control) !=
                       state->stem_mix_sliders.end()) {
          read_mastering_sliders(*state);
          SendMessageW(state->style_combo, CB_SETCURSEL, 6, 0);
        }
      }
      return 0;
    case WM_TIMER:
      if (state && wparam == kUiTimer) update_transport_ui(*state);
      return 0;
    case kJobProgress:
      if (state) SendMessageW(state->progress, PBM_SETPOS, wparam, 0);
      return 0;
    case kAnalysisFinished:
      if (state) finish_generic_job(*state, wparam != 0U,
          L"Analysis complete — findings stored in project history.");
      return 0;
    case kMasterFinished:
      if (state) finish_mastering(*state, wparam != 0U);
      return 0;
    case kRevisionFinished:
      if (state) finish_mastering(*state, wparam != 0U);
      return 0;
    case kExportFinished:
      if (state) finish_generic_job(*state, wparam != 0U,
          L"Export complete — recorded in project history.");
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      const int width = std::max(1, static_cast<int>(client.right - client.left));
      const int height = std::max(1, static_cast<int>(client.bottom - client.top));
      HDC buffered = CreateCompatibleDC(dc);
      HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
      HGDIOBJ previous_bitmap = SelectObject(buffered, bitmap);
      FillRect(buffered, &client, GetSysColorBrush(COLOR_WINDOW));
      if (state) draw_waveform(*state, buffered);
      BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top,
             paint.rcPaint.right - paint.rcPaint.left,
             paint.rcPaint.bottom - paint.rcPaint.top,
             buffered, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
      SelectObject(buffered, previous_bitmap);
      DeleteObject(bitmap);
      DeleteDC(buffered);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_DESTROY:
      KillTimer(window, kUiTimer);
      DragAcceptFiles(window, FALSE);
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      delete state;
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

int run_phase4_self_test(const std::filesystem::path& input,
                         const std::filesystem::path& project_root) {
  amt::codec::SndFileCodecService codecs;
  if (!codecs.available()) return 40;
  amt::project::ProjectStore store(project_root);
  std::string error;
  auto project = store.create(input, error);
  if (project.project_id.empty()) return 41;
  auto report = amt::analysis::analyze_track(codecs, input, error);
  if (!report) return 42;
  project.source_integrated_lufs = report->technical.loudness.integrated_lufs;
  project.analysis_json = amt::analysis::analysis_report_to_json(*report);
  if (!store.append_revision(project, "analysis", "Self-test analysis.", {}, error)) return 43;
  auto plan = amt::mastering::plan_mastering(*report);
  auto rendered = amt::mastering::render_mastering_plan_stereo_self_test(
      codecs, input, project_root / project.project_id / "renders",
      report->technical, plan, error);
  if (!rendered) return 44;
  project.master_a = {.available = true, .path = rendered->master_a.output_path,
                      .integrated_lufs = rendered->master_a.analysis.loudness.integrated_lufs,
                      .true_peak_dbtp = rendered->master_a.analysis.loudness.true_peak_dbtp,
                      .recommended = true};
  project.master_b = {.available = true, .path = rendered->master_b.output_path,
                      .integrated_lufs = rendered->master_b.analysis.loudness.integrated_lufs,
                      .true_peak_dbtp = rendered->master_b.analysis.loudness.true_peak_dbtp,
                      .recommended = false};
  project.master_a_graph_json = plan.master_a.graph.to_json();
  project.master_b_graph_json = plan.master_b.graph.to_json();
  project.selected = amt::project::CandidateSelection::master_a;
  if (!store.append_revision(project, "mastering", "Self-test mastering.",
                             project.master_a.path, error)) return 45;
  const auto loaded = store.load(project.project_id, error);
  if (!loaded || !loaded->master_a.available || !loaded->master_b.available ||
      loaded->analysis_json.empty() || loaded->revisions.size() < 3U) return 46;
  return 0;
}

int run_manual_stem_mix_self_test(const std::filesystem::path& input,
                                  const std::filesystem::path& output_root) {
  amt::codec::SndFileCodecService codecs;
  if (!codecs.available()) return 50;
  std::string error;
  const auto report = amt::analysis::analyze_track(codecs, input, error);
  if (!report) return 51;
  auto plan = amt::mastering::plan_mastering(*report);
  auto controls = amt::mastering::mastering_style_preset(
      amt::mastering::MasteringStyle::balanced);
  controls.stem_mix.drums_db = 1.0;
  controls.stem_mix.bass_db = -1.0;
  controls.stem_mix.vocals_db = 1.5;
  amt::mastering::apply_mastering_controls(plan, controls);
  const auto rendered = amt::mastering::render_mastering_plan(
      codecs, input, output_root / "renders", report->technical, plan, error);
  if (!rendered) return 52;
  if (!std::filesystem::is_regular_file(
          output_root / "manual-stem-mix" / "manual-stem-mix.wav")) {
    return 53;
  }
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  SetUnhandledExceptionFilter(record_unhandled_exception);
  int argument_count = 0;

  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments && argument_count == 4 &&
      std::wstring(arguments[1]) == L"--phase4-self-test") {
    const int result = run_phase4_self_test(arguments[2], arguments[3]);
    LocalFree(arguments);
    return result;
  }
  if (arguments && argument_count == 4 &&
      std::wstring(arguments[1]) == L"--manual-stem-mix-self-test") {
    const int result = run_manual_stem_mix_self_test(arguments[2], arguments[3]);
    LocalFree(arguments);
    return result;
  }
  if (arguments) LocalFree(arguments);

  INITCOMMONCONTROLSEX common{};
  common.dwSize = sizeof(common);
  common.dwICC = ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
  InitCommonControlsEx(&common);

  WNDCLASSW wc{};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.lpszClassName = kWindowClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.hbrBackground = nullptr;
  if (RegisterClassW(&wc) == 0) return 1;

  auto* state = new AppState();
  std::wstring title = L"AudioMasteringTool — ";
  title += widen_utf8(std::string(amt::core::version()));
  HWND window = CreateWindowExW(0, kWindowClass, title.c_str(),
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, 1180, 840, nullptr, nullptr, instance, state);
  if (!window) {
    delete state;
    return 1;
  }
  ShowWindow(window, show_command);
  UpdateWindow(window);

  ACCEL accelerator_entries[] = {
      {static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('O'), kOpenId},
      {static_cast<BYTE>(FVIRTKEY), VK_F5, kAnalyzeId},
      {static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('M'), kMasterId},
      {static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('E'), kExportId},
      {static_cast<BYTE>(FVIRTKEY), VK_ESCAPE, kCancelId},
      {static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('1'), kOriginalId},
      {static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('2'), kMasterAId},
      {static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('3'), kMasterBId},
  };
  HACCEL accelerators = CreateAcceleratorTableW(
      accelerator_entries, static_cast<int>(std::size(accelerator_entries)));

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (accelerators != nullptr &&
        TranslateAcceleratorW(window, accelerators, &message) != 0) {
      continue;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (accelerators != nullptr) DestroyAcceleratorTable(accelerators);
  return static_cast<int>(message.wParam);
}

#else

#include <iostream>
int main() {
  std::cout << "AudioMasteringTool desktop shell is Windows-first.\n";
  return 0;
}

#endif
