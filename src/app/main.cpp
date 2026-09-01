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
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "amt/analysis/FileAnalyzer.h"
#include "amt/codec/AudioIO.h"
#include "amt/codec/SndFileCodec.h"
#include "amt/core/JobControl.h"
#include "amt/core/Version.h"
#include "amt/playback/Transport.h"

namespace {

constexpr wchar_t kWindowClass[] = L"AudioMasteringToolWindow";
constexpr UINT_PTR kUiTimer = 1U;
constexpr int kSeekRange = 10000;
constexpr UINT kAnalysisFinished = WM_APP + 1U;
constexpr UINT kExportFinished = WM_APP + 2U;
constexpr UINT kJobProgress = WM_APP + 3U;

constexpr int kOpenButtonId = 1001;
constexpr int kAnalyzeButtonId = 1002;
constexpr int kPlayButtonId = 1003;
constexpr int kStopButtonId = 1004;
constexpr int kExportButtonId = 1005;
constexpr int kCancelButtonId = 1006;
constexpr int kSeekId = 1101;

struct AppState {
  amt::codec::SndFileCodecService codecs;
  amt::playback::Transport transport{codecs};

  HWND window{nullptr};
  HWND open_button{nullptr};
  HWND analyze_button{nullptr};
  HWND play_button{nullptr};
  HWND stop_button{nullptr};
  HWND export_button{nullptr};
  HWND cancel_button{nullptr};
  HWND seek{nullptr};
  HWND progress{nullptr};
  HWND status{nullptr};
  HWND metrics{nullptr};

  std::filesystem::path source_path;
  std::optional<amt::codec::AudioMetadata> metadata;
  std::optional<amt::analysis::Phase1AnalysisReport> report;
  std::mutex result_mutex;
  std::string worker_error;
  std::thread worker;
  std::shared_ptr<amt::core::CancellationToken> cancellation;
  std::atomic_bool job_running{false};
  bool playable{false};

  ~AppState() {
    if (cancellation) cancellation->cancel();
    if (worker.joinable()) worker.join();
    transport.stop();
  }
};

AppState* state_from(HWND window) {
  return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

std::wstring widen_utf8(const std::string& text) {
  if (text.empty()) return {};
  const int needed = MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) return std::wstring(text.begin(), text.end());
  std::wstring result(static_cast<std::size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      result.data(), needed);
  return result;
}

void set_status(AppState& state, const std::wstring& text) {
  SetWindowTextW(state.status, text.c_str());
}

void show_error(HWND window, const wchar_t* title, const std::string& error) {
  const auto wide = widen_utf8(error);
  MessageBoxW(window, wide.c_str(), title, MB_OK | MB_ICONERROR);
}

void join_completed_worker(AppState& state) {
  if (!state.job_running.load(std::memory_order_acquire) && state.worker.joinable()) {
    state.worker.join();
  }
}

void update_controls(AppState& state) {
  const bool has_source = state.metadata.has_value();
  const bool busy = state.job_running.load(std::memory_order_acquire);
  EnableWindow(state.open_button, !busy);
  EnableWindow(state.analyze_button, has_source && !busy);
  EnableWindow(state.export_button, has_source && !busy);
  EnableWindow(state.cancel_button, busy);
  EnableWindow(state.play_button, has_source && state.playable);
  EnableWindow(state.stop_button, has_source && state.playable);
  EnableWindow(state.seek, has_source && state.playable);
}

void update_play_button(AppState& state) {
  switch (state.transport.state()) {
    case amt::playback::TransportState::playing:
      SetWindowTextW(state.play_button, L"Pause");
      break;
    case amt::playback::TransportState::paused:
      SetWindowTextW(state.play_button, L"Resume");
      break;
    default:
      SetWindowTextW(state.play_button, L"Play");
      break;
  }
}

void update_metrics_text(AppState& state) {
  std::scoped_lock lock(state.result_mutex);
  if (!state.report) {
    SetWindowTextW(state.metrics, L"Analyze the track to populate Phase 1 technical metrics.");
    return;
  }

  const auto& report = *state.report;
  std::wostringstream text;
  text << std::fixed << std::setprecision(2)
       << L"Integrated loudness: " << report.loudness.integrated_lufs << L" LUFS\r\n"
       << L"Maximum momentary: " << report.loudness.max_momentary_lufs << L" LUFS\r\n"
       << L"Maximum short-term: " << report.loudness.max_short_term_lufs << L" LUFS\r\n"
       << L"Loudness range: " << report.loudness.loudness_range_lu << L" LU\r\n"
       << L"Sample peak: " << report.loudness.sample_peak_dbfs << L" dBFS\r\n"
       << L"True peak: " << report.loudness.true_peak_dbtp << L" dBTP\r\n"
       << L"Crest factor: " << report.loudness.crest_factor_db << L" dB\r\n"
       << L"Peak-to-loudness ratio: " << report.loudness.peak_to_loudness_ratio_db << L" dB\r\n"
       << L"Spectral centroid: " << report.spectrum.centroid_hz << L" Hz\r\n"
       << L"85% spectral rolloff: " << report.spectrum.rolloff_85_hz << L" Hz\r\n"
       << L"Stereo correlation: " << report.stereo.correlation << L"\r\n"
       << L"Low / mid / high width: " << report.stereo.low_band_width << L" / "
       << report.stereo.mid_band_width << L" / " << report.stereo.high_band_width << L"\r\n"
       << L"Mono fold-down delta: " << report.stereo.mono_fold_down_delta_db << L" dB\r\n"
       << L"NaN / Inf samples: " << report.integrity.nan_samples << L" / "
       << report.integrity.infinite_samples << L"\r\n"
       << L"Clipped samples: " << report.integrity.clipped_samples << L"\r\n"
       << L"Longest full-scale run: " << report.integrity.longest_full_scale_run << L" samples\r\n"
       << L"Maximum DC offset: " << report.integrity.max_absolute_dc_offset << L"\r\n"
       << L"Channel imbalance: " << report.integrity.channel_imbalance_db << L" dB\r\n"
       << L"Head / tail silence: " << report.integrity.head_silence_seconds << L" / "
       << report.integrity.tail_silence_seconds << L" s\r\n"
       << L"Waveform cache levels: " << report.waveform.levels.size();
  SetWindowTextW(state.metrics, text.str().c_str());
}

void layout_controls(AppState& state) {
  RECT client{};
  GetClientRect(state.window, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  constexpr int margin = 12;
  constexpr int button_height = 30;
  constexpr int button_width = 92;
  constexpr int gap = 8;

  int x = margin;
  for (HWND button : {state.open_button, state.analyze_button, state.play_button,
                      state.stop_button, state.export_button, state.cancel_button}) {
    MoveWindow(button, x, margin, button_width, button_height, TRUE);
    x += button_width + gap;
  }

  MoveWindow(state.status, margin, 50, std::max(10, width - 2 * margin), 24, TRUE);
  const int waveform_top = 80;
  const int waveform_height = std::max(120, std::min(260, height / 3));
  MoveWindow(state.seek, margin, waveform_top + waveform_height + 5,
             std::max(10, width - 2 * margin), 32, TRUE);
  MoveWindow(state.progress, margin, waveform_top + waveform_height + 40,
             std::max(10, width - 2 * margin), 14, TRUE);
  const int metrics_top = waveform_top + waveform_height + 62;
  MoveWindow(state.metrics, margin, metrics_top, std::max(10, width - 2 * margin),
             std::max(50, height - metrics_top - margin), TRUE);
}

RECT waveform_rect(HWND window) {
  RECT client{};
  GetClientRect(window, &client);
  const int height = client.bottom - client.top;
  const int waveform_height = std::max(120, std::min(260, height / 3));
  return RECT{12, 80, std::max(13L, client.right - 12), 80 + waveform_height};
}

void draw_waveform(AppState& state, HDC dc) {
  const RECT area = waveform_rect(state.window);
  HBRUSH background = CreateSolidBrush(RGB(24, 27, 32));
  FillRect(dc, &area, background);
  DeleteObject(background);

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(205, 211, 220));

  std::scoped_lock lock(state.result_mutex);
  if (!state.report || state.report->waveform.levels.empty()) {
    RECT text_area = area;
    DrawTextW(dc, L"Waveform will appear after analysis.", -1, &text_area,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }

  const auto& levels = state.report->waveform.levels;
  const int width = std::max(1, area.right - area.left);
  const amt::audio::WaveformLevel* level = &levels.back();
  for (const auto& candidate : levels) {
    if (!candidate.channels.empty() &&
        candidate.channels.front().size() <= static_cast<std::size_t>(width * 2)) {
      level = &candidate;
      break;
    }
  }
  if (level->channels.empty() || level->channels.front().empty()) return;

  const std::size_t displayed_channels = std::min<std::size_t>(2U, level->channels.size());
  HPEN center_pen = CreatePen(PS_SOLID, 1, RGB(65, 70, 80));
  HPEN wave_pen = CreatePen(PS_SOLID, 1, RGB(71, 197, 214));
  HPEN previous = static_cast<HPEN>(SelectObject(dc, center_pen));

  const int total_height = area.bottom - area.top;
  for (std::size_t channel = 0; channel < displayed_channels; ++channel) {
    const int channel_top = area.top +
        static_cast<int>((static_cast<std::int64_t>(total_height) * channel) /
                         static_cast<std::int64_t>(displayed_channels));
    const int channel_bottom = area.top +
        static_cast<int>((static_cast<std::int64_t>(total_height) * (channel + 1U)) /
                         static_cast<std::int64_t>(displayed_channels));
    const int center = (channel_top + channel_bottom) / 2;
    MoveToEx(dc, area.left, center, nullptr);
    LineTo(dc, area.right, center);

    SelectObject(dc, wave_pen);
    const auto& bins = level->channels[channel];
    const double scale = static_cast<double>(channel_bottom - channel_top) * 0.45;
    for (int pixel = 0; pixel < width; ++pixel) {
      const auto index = std::min<std::size_t>(
          bins.size() - 1U,
          static_cast<std::size_t>((static_cast<std::uint64_t>(pixel) * bins.size()) /
                                   static_cast<std::uint64_t>(width)));
      const auto& bin = bins[index];
      const double minimum = std::clamp(static_cast<double>(bin.minimum), -1.2, 1.2);
      const double maximum = std::clamp(static_cast<double>(bin.maximum), -1.2, 1.2);
      const int y_min = center - static_cast<int>(std::lround(maximum * scale));
      const int y_max = center - static_cast<int>(std::lround(minimum * scale));
      MoveToEx(dc, area.left + pixel, y_min, nullptr);
      LineTo(dc, area.left + pixel, y_max + 1);
    }
    SelectObject(dc, center_pen);
  }

  SelectObject(dc, previous);
  DeleteObject(center_pen);
  DeleteObject(wave_pen);

  if (state.metadata && state.metadata->frames > 0) {
    const auto frame = std::clamp<std::int64_t>(
        state.transport.playhead_frame(), 0, state.metadata->frames);
    const int x = area.left + static_cast<int>(
        (static_cast<long double>(frame) * width) /
        static_cast<long double>(state.metadata->frames));
    HPEN playhead_pen = CreatePen(PS_SOLID, 1, RGB(240, 240, 240));
    previous = static_cast<HPEN>(SelectObject(dc, playhead_pen));
    MoveToEx(dc, x, area.top, nullptr);
    LineTo(dc, x, area.bottom);
    SelectObject(dc, previous);
    DeleteObject(playhead_pen);
  }
}

bool load_source(AppState& state, const std::filesystem::path& path) {
  std::string error;
  const auto metadata = state.codecs.probe(path, error);
  if (!metadata) {
    show_error(state.window, L"Unable to load audio", error);
    return false;
  }

  state.transport.stop();
  state.playable = metadata->channels >= 1 && metadata->channels <= 2;
  if (state.playable && !state.transport.load(path, error)) {
    show_error(state.window, L"Unable to initialize playback", error);
    return false;
  }

  {
    std::scoped_lock lock(state.result_mutex);
    state.report.reset();
    state.worker_error.clear();
  }
  state.source_path = path;
  state.metadata = *metadata;
  SendMessageW(state.seek, TBM_SETPOS, TRUE, 0);
  SendMessageW(state.progress, PBM_SETPOS, 0, 0);
  update_metrics_text(state);
  update_controls(state);
  update_play_button(state);

  std::wostringstream status;
  status << L"Loaded: " << path.filename().wstring() << L" — "
         << metadata->sample_rate << L" Hz, " << metadata->channels << L" ch, "
         << metadata->bit_depth << L" bit";
  if (!state.playable) status << L" — analysis/export available; native playback is mono/stereo in Phase 1";
  set_status(state, status.str());
  InvalidateRect(state.window, nullptr, FALSE);
  return true;
}

void choose_source(AppState& state) {
  wchar_t path_buffer[32768]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = state.window;
  dialog.lpstrFilter = L"Supported audio (*.wav;*.wave;*.aif;*.aiff;*.flac)\0*.wav;*.wave;*.aif;*.aiff;*.flac\0All files (*.*)\0*.*\0\0";
  dialog.lpstrFile = path_buffer;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path_buffer));
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetOpenFileNameW(&dialog) != FALSE) load_source(state, path_buffer);
}

void begin_analysis(AppState& state) {
  if (!state.metadata || state.job_running.exchange(true, std::memory_order_acq_rel)) return;
  join_completed_worker(state);
  auto cancellation = std::make_shared<amt::core::CancellationToken>();
  state.cancellation = cancellation;
  {
    std::scoped_lock lock(state.result_mutex);
    state.worker_error.clear();
  }
  update_controls(state);
  set_status(state, L"Analyzing audio…");
  SendMessageW(state.progress, PBM_SETPOS, 0, 0);

  const auto source = state.source_path;
  HWND window = state.window;
  AppState* state_pointer = &state;
  state.worker = std::thread([source, window, state_pointer, cancellation] {
    amt::codec::SndFileCodecService codecs;
    std::string error;
    auto report = amt::analysis::analyze_file(
        codecs, source, error, cancellation.get(),
        [window](const double progress) {
          const auto value = static_cast<WPARAM>(
              std::clamp(static_cast<int>(std::lround(progress * 1000.0)), 0, 1000));
          PostMessageW(window, kJobProgress, value, 0);
        });
    {
      std::scoped_lock lock(state_pointer->result_mutex);
      if (report) state_pointer->report = std::move(*report);
      state_pointer->worker_error = std::move(error);
    }
    state_pointer->job_running.store(false, std::memory_order_release);
    PostMessageW(window, kAnalysisFinished, report.has_value() ? 1U : 0U, 0);
  });
}

std::optional<std::filesystem::path> choose_export_path(AppState& state) {
  wchar_t path_buffer[32768]{};
  std::wstring suggested = state.source_path.stem().wstring() + L"_pass-through.wav";
  const auto copy_count = std::min<std::size_t>(suggested.size(), std::size(path_buffer) - 1U);
  std::copy_n(suggested.c_str(), copy_count, path_buffer);

  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = state.window;
  dialog.lpstrFilter = L"WAV (*.wav)\0*.wav\0AIFF (*.aiff)\0*.aiff\0FLAC (*.flac)\0*.flac\0\0";
  dialog.lpstrFile = path_buffer;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path_buffer));
  dialog.lpstrDefExt = L"wav";
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetSaveFileNameW(&dialog) == FALSE) return std::nullopt;
  return std::filesystem::path(path_buffer);
}

void begin_export(AppState& state) {
  if (!state.metadata || state.job_running.load(std::memory_order_acquire)) return;
  const auto output = choose_export_path(state);
  if (!output || state.job_running.exchange(true, std::memory_order_acq_rel)) return;
  join_completed_worker(state);
  auto cancellation = std::make_shared<amt::core::CancellationToken>();
  state.cancellation = cancellation;
  {
    std::scoped_lock lock(state.result_mutex);
    state.worker_error.clear();
  }
  update_controls(state);
  set_status(state, L"Exporting transparent pass-through…");
  SendMessageW(state.progress, PBM_SETPOS, 0, 0);

  const auto source = state.source_path;
  HWND window = state.window;
  AppState* state_pointer = &state;
  state.worker = std::thread([source, output = *output, window, state_pointer, cancellation] {
    amt::codec::SndFileCodecService codecs;
    std::string error;
    const bool success = amt::codec::export_audio(
        codecs, source, output, {}, error, cancellation.get(),
        [window](const double progress) {
          const auto value = static_cast<WPARAM>(
              std::clamp(static_cast<int>(std::lround(progress * 1000.0)), 0, 1000));
          PostMessageW(window, kJobProgress, value, 0);
        });
    {
      std::scoped_lock lock(state_pointer->result_mutex);
      state_pointer->worker_error = std::move(error);
    }
    state_pointer->job_running.store(false, std::memory_order_release);
    PostMessageW(window, kExportFinished, success ? 1U : 0U, 0);
  });
}

void toggle_playback(AppState& state) {
  if (!state.playable) return;
  std::string error;
  bool success = true;
  switch (state.transport.state()) {
    case amt::playback::TransportState::playing:
      success = state.transport.pause(error);
      break;
    case amt::playback::TransportState::paused:
      success = state.transport.resume(error);
      break;
    default:
      success = state.transport.play(error);
      break;
  }
  if (!success) show_error(state.window, L"Playback error", error);
  update_play_button(state);
}

void seek_from_control(AppState& state) {
  if (!state.metadata || state.metadata->frames <= 0 || !state.playable) return;
  const auto normalized = static_cast<int>(SendMessageW(state.seek, TBM_GETPOS, 0, 0));
  const auto frame = static_cast<std::int64_t>(
      (static_cast<long double>(normalized) * state.metadata->frames) /
      static_cast<long double>(kSeekRange));
  std::string error;
  if (!state.transport.seek(frame, error)) show_error(state.window, L"Seek error", error);
  update_play_button(state);
  InvalidateRect(state.window, nullptr, FALSE);
}

void cancel_job(AppState& state) {
  if (state.cancellation) {
    state.cancellation->cancel();
    set_status(state, L"Cancelling…");
  }
}

void finish_job(AppState& state, const bool success, const wchar_t* success_status) {
  join_completed_worker(state);
  state.cancellation.reset();
  SendMessageW(state.progress, PBM_SETPOS, success ? 1000 : 0, 0);
  std::string error;
  {
    std::scoped_lock lock(state.result_mutex);
    error = state.worker_error;
  }
  if (success) {
    set_status(state, success_status);
  } else if (error == "analysis cancelled" || error == "export cancelled") {
    set_status(state, L"Operation cancelled.");
  } else {
    set_status(state, L"Operation failed.");
    show_error(state.window, L"Audio operation failed", error);
  }
  update_controls(state);
  update_metrics_text(state);
  InvalidateRect(state.window, nullptr, FALSE);
}

void update_transport_ui(AppState& state) {
  update_play_button(state);
  if (!state.metadata || state.metadata->frames <= 0 || !state.playable) return;
  const auto frame = std::clamp<std::int64_t>(
      state.transport.playhead_frame(), 0, state.metadata->frames);
  const int position = static_cast<int>(
      (static_cast<long double>(frame) * kSeekRange) /
      static_cast<long double>(state.metadata->frames));
  SendMessageW(state.seek, TBM_SETPOS, TRUE, position);
  InvalidateRect(state.window, nullptr, FALSE);
}

void create_controls(AppState& state) {
  const DWORD button_style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
  state.open_button = CreateWindowExW(0, L"BUTTON", L"Open", button_style,
                                      0, 0, 0, 0, state.window,
                                      reinterpret_cast<HMENU>(kOpenButtonId), nullptr, nullptr);
  state.analyze_button = CreateWindowExW(0, L"BUTTON", L"Analyze", button_style,
                                         0, 0, 0, 0, state.window,
                                         reinterpret_cast<HMENU>(kAnalyzeButtonId), nullptr, nullptr);
  state.play_button = CreateWindowExW(0, L"BUTTON", L"Play", button_style,
                                      0, 0, 0, 0, state.window,
                                      reinterpret_cast<HMENU>(kPlayButtonId), nullptr, nullptr);
  state.stop_button = CreateWindowExW(0, L"BUTTON", L"Stop", button_style,
                                      0, 0, 0, 0, state.window,
                                      reinterpret_cast<HMENU>(kStopButtonId), nullptr, nullptr);
  state.export_button = CreateWindowExW(0, L"BUTTON", L"Export", button_style,
                                        0, 0, 0, 0, state.window,
                                        reinterpret_cast<HMENU>(kExportButtonId), nullptr, nullptr);
  state.cancel_button = CreateWindowExW(0, L"BUTTON", L"Cancel", button_style,
                                        0, 0, 0, 0, state.window,
                                        reinterpret_cast<HMENU>(kCancelButtonId), nullptr, nullptr);
  state.status = CreateWindowExW(0, L"STATIC", L"Open a WAV, AIFF, or FLAC file.",
                                 WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
                                 state.window, nullptr, nullptr, nullptr);
  state.seek = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                               WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                               0, 0, 0, 0, state.window,
                               reinterpret_cast<HMENU>(kSeekId), nullptr, nullptr);
  SendMessageW(state.seek, TBM_SETRANGE, TRUE, MAKELONG(0, kSeekRange));
  state.progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                   0, 0, 0, 0, state.window, nullptr, nullptr, nullptr);
  SendMessageW(state.progress, PBM_SETRANGE32, 0, 1000);
  state.metrics = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"Analyze the track to populate Phase 1 technical metrics.",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
      0, 0, 0, 0, state.window, nullptr, nullptr, nullptr);
  update_controls(state);
  layout_controls(state);
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
      if (state != nullptr) {
        create_controls(*state);
        SetTimer(window, kUiTimer, 100U, nullptr);
      }
      return 0;
    case WM_SIZE:
      if (state != nullptr) layout_controls(*state);
      return 0;
    case WM_COMMAND:
      if (state == nullptr || HIWORD(wparam) != BN_CLICKED) break;
      switch (LOWORD(wparam)) {
        case kOpenButtonId:
          choose_source(*state);
          return 0;
        case kAnalyzeButtonId:
          begin_analysis(*state);
          return 0;
        case kPlayButtonId:
          toggle_playback(*state);
          return 0;
        case kStopButtonId:
          state->transport.stop();
          update_play_button(*state);
          return 0;
        case kExportButtonId:
          begin_export(*state);
          return 0;
        case kCancelButtonId:
          cancel_job(*state);
          return 0;
        default:
          break;
      }
      break;
    case WM_HSCROLL:
      if (state != nullptr && reinterpret_cast<HWND>(lparam) == state->seek) {
        const auto code = LOWORD(wparam);
        if (code == TB_ENDTRACK || code == TB_THUMBPOSITION) seek_from_control(*state);
      }
      return 0;
    case WM_TIMER:
      if (state != nullptr && wparam == kUiTimer) update_transport_ui(*state);
      return 0;
    case kJobProgress:
      if (state != nullptr) SendMessageW(state->progress, PBM_SETPOS, wparam, 0);
      return 0;
    case kAnalysisFinished:
      if (state != nullptr) finish_job(*state, wparam != 0U, L"Analysis complete.");
      return 0;
    case kExportFinished:
      if (state != nullptr) finish_job(*state, wparam != 0U, L"Transparent export complete.");
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      if (state != nullptr) draw_waveform(*state, dc);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DESTROY:
      KillTimer(window, kUiTimer);
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      delete state;
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

int run_phase1_self_test(const std::filesystem::path& input,
                         const std::filesystem::path& output) {
  amt::codec::SndFileCodecService codecs;
  if (!codecs.available()) return 10;
  std::string error;
  const auto report = amt::analysis::analyze_file(codecs, input, error);
  if (!report || report->waveform.levels.empty() || report->waveform.source_frames <= 0 ||
      !std::isfinite(report->loudness.integrated_lufs) ||
      !std::isfinite(report->loudness.true_peak_dbtp)) {
    return 11;
  }
  if (!amt::codec::export_audio(codecs, input, output, {}, error)) return 12;
  if (!amt::codec::verify_audio_equal(codecs, input, output, 2.0e-7, error)) return 13;
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  int argument_count = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments != nullptr && argument_count == 4 &&
      std::wstring(arguments[1]) == L"--phase1-self-test") {
    const int result = run_phase1_self_test(arguments[2], arguments[3]);
    LocalFree(arguments);
    return result;
  }
  if (arguments != nullptr) LocalFree(arguments);

  INITCOMMONCONTROLSEX controls{};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
  InitCommonControlsEx(&controls);

  WNDCLASSW window_class{};
  window_class.lpfnWndProc = window_proc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kWindowClass;
  window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (RegisterClassW(&window_class) == 0) return 1;

  auto* state = new AppState();
  std::wstring title = L"AudioMasteringTool — Phase 1 — ";
  title += widen_utf8(std::string(amt::core::version()));
  HWND window = CreateWindowExW(
      0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1080, 760, nullptr, nullptr, instance, state);
  if (window == nullptr) {
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
