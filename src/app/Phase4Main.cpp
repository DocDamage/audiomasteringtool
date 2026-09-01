#ifdef _WIN32

#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
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

namespace {

constexpr wchar_t kWindowClass[] = L"AudioMasteringToolPhase4Window";
constexpr UINT_PTR kUiTimer = 1U;
constexpr int kSeekRange = 10000;
constexpr UINT kAnalysisFinished = WM_APP + 1U;
constexpr UINT kMasterFinished = WM_APP + 2U;
constexpr UINT kExportFinished = WM_APP + 3U;
constexpr UINT kJobProgress = WM_APP + 4U;

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
constexpr UINT kRecentMenuBase = 40000U;

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
  HWND play_button{nullptr};
  HWND stop_button{nullptr};
  HWND original_button{nullptr};
  HWND master_a_button{nullptr};
  HWND master_b_button{nullptr};
  HWND recipe_combo{nullptr};
  HWND seek{nullptr};
  HWND progress{nullptr};
  HWND status{nullptr};
  HWND details{nullptr};

  std::filesystem::path source_path;
  std::optional<amt::codec::AudioMetadata> metadata;
  std::optional<amt::analysis::AnalysisReport> analysis;
  std::optional<amt::mastering::MasteringPlan> plan;
  std::optional<amt::mastering::MasteringRenderPair> rendered;
  std::optional<amt::project::ProjectRecord> project;

  std::mutex data_mutex;
  std::thread worker;
  std::shared_ptr<amt::core::CancellationToken> cancellation;
  std::atomic_bool busy{false};
  std::string worker_error;
  bool playable{false};
  bool comparison_ready{false};

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
  SetWindowTextW(state.play_button, text);
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
  EnableWindow(state.play_button, has_source && state.playable);
  EnableWindow(state.stop_button, has_source && state.playable);
  EnableWindow(state.seek, has_source && state.playable);
  EnableWindow(state.original_button, has_masters && state.comparison_ready && state.playable);
  EnableWindow(state.master_a_button, has_masters && state.comparison_ready && state.playable);
  EnableWindow(state.master_b_button, has_masters && state.comparison_ready && state.playable);
}

void update_details(AppState& state) {
  std::scoped_lock lock(state.data_mutex);
  std::wostringstream text;
  text << std::fixed << std::setprecision(2);
  if (!state.project) {
    text << L"Drop a WAV, AIFF, or FLAC file here, or choose Open.\r\n"
         << L"The normal workflow is Analyze → Master → loudness-matched Original/A/B → Export.";
    SetWindowTextW(state.details, text.str().c_str());
    return;
  }

  const auto& project = *state.project;
  text << L"PROJECT\r\n"
       << widen_utf8(project.display_name) << L"\r\n"
       << L"History nodes: " << project.revisions.size() << L"\r\n"
       << L"Selected: " << selection_label(project.selected) << L"\r\n";

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
  const int waveform_height = std::max(120, std::min(230, height / 3));
  return RECT{12, 122, std::max<LONG>(13, client.right - 12),
              static_cast<LONG>(122 + waveform_height)};
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
  constexpr int gap = 7;
  constexpr int button_height = 30;
  constexpr int button_width = 86;
  int x = margin;
  for (HWND button : {state.open_button, state.recent_button, state.analyze_button,
                      state.master_button, state.export_button, state.cancel_button}) {
    MoveWindow(button, x, margin, button_width, button_height, TRUE);
    x += button_width + gap;
  }
  MoveWindow(state.recipe_combo, x, margin, std::max(170, width - x - margin), 300, TRUE);

  x = margin;
  for (HWND button : {state.play_button, state.stop_button, state.original_button,
                      state.master_a_button, state.master_b_button}) {
    const int current_width = button == state.master_a_button ? 150 : 96;
    MoveWindow(button, x, 49, current_width, button_height, TRUE);
    x += current_width + gap;
  }
  MoveWindow(state.status, margin, 88, std::max(10, width - 2 * margin), 25, TRUE);

  const RECT wave = waveform_rect(state.window);
  MoveWindow(state.seek, margin, wave.bottom + 5, std::max(10, width - 2 * margin), 30, TRUE);
  MoveWindow(state.progress, margin, wave.bottom + 38, std::max(10, width - 2 * margin), 14, TRUE);
  const int details_top = static_cast<int>(wave.bottom) + 60;
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
  dialog.lpstrFilter = L"Supported audio (*.wav;*.wave;*.aif;*.aiff;*.flac)\0*.wav;*.wave;*.aif;*.aiff;*.flac\0All files (*.*)\0*.*\0\0";
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
  HWND window = state.window;
  AppState* state_pointer = &state;
  state.worker = std::thread([source, output_directory, report = std::move(report),
                              project_snapshot = std::move(project_snapshot), cancellation,
                              window, state_pointer]() mutable {
    amt::codec::SndFileCodecService codecs;
    std::string error;
    auto plan = amt::mastering::plan_mastering(report);
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
  SendMessageW(state.seek, TBM_SETPOS, TRUE, position);
  InvalidateRect(state.window, nullptr, FALSE);
}

void create_controls(AppState& state) {
  constexpr DWORD button = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
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
  state.recipe_combo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0,
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
  state.status = CreateWindowExW(0, L"STATIC",
      L"Drop audio here or choose Open. Projects are saved locally automatically.",
      WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, state.window, nullptr, nullptr, nullptr);
  state.seek = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
      WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0, 0, 0,
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
      if (!state || HIWORD(wparam) != BN_CLICKED) break;
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
        default: break;
      }
      break;
    case WM_HSCROLL:
      if (state && reinterpret_cast<HWND>(lparam) == state->seek) {
        const auto code = LOWORD(wparam);
        if (code == TB_ENDTRACK || code == TB_THUMBPOSITION) seek_from_ui(*state);
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
    case kExportFinished:
      if (state) finish_generic_job(*state, wparam != 0U,
          L"Export complete — recorded in project history.");
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      if (state) draw_waveform(*state, dc);
      EndPaint(window, &paint);
      return 0;
    }
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
  auto rendered = amt::mastering::render_mastering_plan(
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

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  int argument_count = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments && argument_count == 4 &&
      std::wstring(arguments[1]) == L"--phase4-self-test") {
    const int result = run_phase4_self_test(arguments[2], arguments[3]);
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
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (RegisterClassW(&wc) == 0) return 1;

  auto* state = new AppState();
  std::wstring title = L"AudioMasteringTool — ";
  title += widen_utf8(std::string(amt::core::version()));
  HWND window = CreateWindowExW(0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1180, 840, nullptr, nullptr, instance, state);
  if (!window) {
    delete state;
    return 1;
  }
  ShowWindow(window, show_command);
  UpdateWindow(window);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

#else

#include <iostream>
int main() {
  std::cout << "AudioMasteringTool desktop shell is Windows-first.\n";
  return 0;
}

#endif
