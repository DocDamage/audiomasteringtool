#include "amt/separation/WorkerSeparationProvider.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "amt/codec/SndFileCodec.h"
#include "amt/core/FileFingerprint.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace amt::separation {
namespace {

std::atomic<std::uint64_t> g_worker_output_counter{0U};

[[nodiscard]] char ascii_lower(const char value) noexcept {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value - 'A' + 'a');
  }
  return value;
}

[[nodiscard]] bool ascii_equal_ignore_case(const std::string_view first,
                                           const std::string_view second) noexcept {
  if (first.size() != second.size()) return false;
  for (std::size_t index = 0U; index < first.size(); ++index) {
    if (ascii_lower(first[index]) != ascii_lower(second[index])) return false;
  }
  return true;
}

[[nodiscard]] std::string lower_ascii_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const char c) {
    return ascii_lower(c);
  });
  return value;
}

[[nodiscard]] bool valid_contract(
    const WorkerSeparationProviderConfig& config) noexcept {
  if (config.worker_executable.empty() || config.model_artifact.empty() ||
      config.manifest.model_name.empty() || config.manifest.model_version.empty() ||
      config.manifest.model_sha256.size() != 64U ||
      config.manifest.expected_input_sample_rate <= 0 ||
      config.manifest.stem_taxonomy.empty()) {
    return false;
  }
  if (config.contract.input_tensor_name.empty() ||
      config.contract.output_tensor_name.empty() ||
      config.contract.chunk_frames < 4096U ||
      config.contract.chunk_frames > 2097152U ||
      config.contract.overlap_frames * 2U >= config.contract.chunk_frames ||
      !std::isfinite(config.contract.calibrated_output_confidence) ||
      config.contract.calibrated_output_confidence <= 0.0 ||
      config.contract.calibrated_output_confidence > 1.0 ||
      config.maximum_worker_output_bytes < 1024U ||
      config.maximum_runtime_seconds == 0U) {
    return false;
  }

  for (const auto role : config.manifest.stem_taxonomy) {
    if (role == StemRole::unknown) return false;
  }
  for (std::size_t index = 0U; index < config.manifest.stem_taxonomy.size(); ++index) {
    if (std::find(config.manifest.stem_taxonomy.begin() +
                      static_cast<std::ptrdiff_t>(index + 1U),
                  config.manifest.stem_taxonomy.end(),
                  config.manifest.stem_taxonomy[index]) !=
        config.manifest.stem_taxonomy.end()) {
      return false;
    }
  }

  if (!ascii_equal_ignore_case(config.execution_provider, "cpu") &&
      !ascii_equal_ignore_case(config.execution_provider, "cuda")) {
    return false;
  }
  return std::any_of(
      config.manifest.supported_execution_providers.begin(),
      config.manifest.supported_execution_providers.end(),
      [&](const std::string& value) {
        return ascii_equal_ignore_case(value, config.execution_provider);
      });
}

[[nodiscard]] bool role_is_supported(const SeparationModelManifest& manifest,
                                     const StemRole role) {
  return role != StemRole::unknown &&
         std::find(manifest.stem_taxonomy.begin(), manifest.stem_taxonomy.end(), role) !=
             manifest.stem_taxonomy.end();
}

[[nodiscard]] std::vector<StemRole> selected_roles(
    const SeparationRequest& request,
    const SeparationModelManifest& manifest,
    std::string& error) {
  if (request.requested_stems.empty()) return manifest.stem_taxonomy;

  std::vector<StemRole> roles;
  roles.reserve(request.requested_stems.size());
  for (const auto role : request.requested_stems) {
    if (!role_is_supported(manifest, role)) {
      error = "requested source role is not present in the configured model taxonomy: " +
              stem_role_name(role);
      return {};
    }
    if (std::find(roles.begin(), roles.end(), role) == roles.end()) {
      roles.push_back(role);
    }
  }
  return roles;
}

[[nodiscard]] std::string stem_csv(const SeparationModelManifest& manifest) {
  std::string output;
  for (std::size_t index = 0U; index < manifest.stem_taxonomy.size(); ++index) {
    if (index > 0U) output.push_back(',');
    output += stem_role_name(manifest.stem_taxonomy[index]);
  }
  return output;
}

[[nodiscard]] std::filesystem::path fallback_output_directory(
    const WorkerSeparationProviderConfig& config) {
  auto root = config.fallback_output_root;
  if (root.empty()) {
    root = std::filesystem::temp_directory_path() / "AudioMasteringTool" /
           "source-estimates";
  }
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto sequence = g_worker_output_counter.fetch_add(1U, std::memory_order_relaxed);
  return root / ("estimate-" + std::to_string(ticks) + "-" +
                 std::to_string(sequence));
}

[[nodiscard]] std::optional<std::int64_t> extract_int64(
    const std::string& json,
    const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const auto key_position = json.find(needle);
  if (key_position == std::string::npos) return std::nullopt;
  const auto colon = json.find(':', key_position + needle.size());
  if (colon == std::string::npos) return std::nullopt;
  std::size_t position = colon + 1U;
  while (position < json.size() &&
         std::isspace(static_cast<unsigned char>(json[position]))) {
    ++position;
  }
  bool negative = false;
  if (position < json.size() && json[position] == '-') {
    negative = true;
    ++position;
  }
  if (position >= json.size() ||
      !std::isdigit(static_cast<unsigned char>(json[position]))) {
    return std::nullopt;
  }
  std::uint64_t value = 0U;
  while (position < json.size() &&
         std::isdigit(static_cast<unsigned char>(json[position]))) {
    const auto digit = static_cast<std::uint64_t>(json[position] - '0');
    if (value > (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) -
                 digit) /
                    10U) {
      return std::nullopt;
    }
    value = value * 10U + digit;
    ++position;
  }
  const auto signed_value = static_cast<std::int64_t>(value);
  return negative ? -signed_value : signed_value;
}

[[nodiscard]] std::optional<std::string> extract_json_string(
    const std::string& json,
    const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const auto key_position = json.find(needle);
  if (key_position == std::string::npos) return std::nullopt;
  const auto colon = json.find(':', key_position + needle.size());
  if (colon == std::string::npos) return std::nullopt;
  auto position = json.find('"', colon + 1U);
  if (position == std::string::npos) return std::nullopt;
  ++position;

  std::string output;
  bool escaped = false;
  for (; position < json.size(); ++position) {
    const char c = json[position];
    if (escaped) {
      switch (c) {
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case '\\': output.push_back('\\'); break;
        case '"': output.push_back('"'); break;
        default: output.push_back(c); break;
      }
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') return output;
    output.push_back(c);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> final_worker_json(
    const std::string& output) {
  const auto success = output.rfind("{\"ok\":true");
  const auto failure = output.rfind("{\"ok\":false");
  const auto position = success == std::string::npos
      ? failure
      : (failure == std::string::npos ? success : std::max(success, failure));
  if (position == std::string::npos) return std::nullopt;
  const auto line_end = output.find('\n', position);
  return output.substr(position, line_end == std::string::npos
                                     ? std::string::npos
                                     : line_end - position);
}

[[nodiscard]] std::optional<int> latest_worker_progress(
    const std::string& output) {
  const auto position = output.rfind("{\"progress\":");
  if (position == std::string::npos) return std::nullopt;
  const auto value = extract_int64(output.substr(position), "progress");
  if (!value || *value < 0 || *value > 100) return std::nullopt;
  return static_cast<int>(*value);
}

#ifdef _WIN32

struct UniqueHandle {
  HANDLE value{nullptr};

  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE handle) : value(handle) {}
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  UniqueHandle(UniqueHandle&& other) noexcept : value(other.value) {
    other.value = nullptr;
  }
  UniqueHandle& operator=(UniqueHandle&& other) noexcept {
    if (this == &other) return *this;
    reset();
    value = other.value;
    other.value = nullptr;
    return *this;
  }
  ~UniqueHandle() { reset(); }
  void reset(HANDLE handle = nullptr) noexcept {
    if (value != nullptr && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    value = handle;
  }
  [[nodiscard]] HANDLE get() const noexcept { return value; }
};

[[nodiscard]] std::wstring wide_utf8(const std::string& text) {
  if (text.empty() ||
      text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return {};
  }
  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0);
  if (required <= 0) return {};
  std::wstring output(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), output.data(),
                          required) != required) {
    return {};
  }
  return output;
}

[[nodiscard]] std::wstring quote_argument(const std::wstring& argument) {
  std::wstring output;
  output.push_back(L'"');
  std::size_t backslashes = 0U;
  for (const wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      output.append(backslashes * 2U + 1U, L'\\');
      output.push_back(L'"');
      backslashes = 0U;
      continue;
    }
    output.append(backslashes, L'\\');
    backslashes = 0U;
    output.push_back(c);
  }
  output.append(backslashes * 2U, L'\\');
  output.push_back(L'"');
  return output;
}

[[nodiscard]] std::optional<std::wstring> worker_command_line(
    const WorkerSeparationProviderConfig& config,
    const SeparationRequest& request,
    const std::filesystem::path& output_directory,
    std::string& error) {
  const auto stems = wide_utf8(stem_csv(config.manifest));
  const auto provider = wide_utf8(lower_ascii_copy(config.execution_provider));
  const auto input_tensor = wide_utf8(config.contract.input_tensor_name);
  const auto output_tensor = wide_utf8(config.contract.output_tensor_name);
  if (stems.empty() || provider.empty() || input_tensor.empty() ||
      output_tensor.empty()) {
    error = "separation worker contract contains invalid UTF-8 text";
    return std::nullopt;
  }

  std::vector<std::wstring> arguments{
      config.worker_executable.wstring(),
      L"--separate-onnx",
      L"--model", config.model_artifact.wstring(),
      L"--source", request.source_path.wstring(),
      L"--output", output_directory.wstring(),
      L"--sample-rate", std::to_wstring(config.manifest.expected_input_sample_rate),
      L"--stems", stems,
      L"--provider", provider,
      L"--input-tensor", input_tensor,
      L"--output-tensor", output_tensor,
      L"--chunk-frames", std::to_wstring(config.contract.chunk_frames),
      L"--overlap-frames", std::to_wstring(config.contract.overlap_frames)};

  std::wstring command;
  for (std::size_t index = 0U; index < arguments.size(); ++index) {
    if (index > 0U) command.push_back(L' ');
    command += quote_argument(arguments[index]);
  }
  return command;
}

bool drain_pipe(HANDLE pipe,
                std::string& output,
                const std::size_t limit,
                std::string& error) {
  while (true) {
    DWORD available = 0U;
    if (!PeekNamedPipe(pipe, nullptr, 0U, nullptr, &available, nullptr)) {
      const DWORD code = GetLastError();
      if (code == ERROR_BROKEN_PIPE) return true;
      error = "unable to inspect separation worker output pipe";
      return false;
    }
    if (available == 0U) return true;

    char buffer[4096];
    DWORD read = 0U;
    if (!ReadFile(pipe, buffer,
                  static_cast<DWORD>(std::min<std::size_t>(sizeof(buffer), available)),
                  &read, nullptr)) {
      const DWORD code = GetLastError();
      if (code == ERROR_BROKEN_PIPE) return true;
      error = "unable to read separation worker output";
      return false;
    }
    if (output.size() + static_cast<std::size_t>(read) > limit) {
      error = "separation worker exceeded the bounded diagnostic output limit";
      return false;
    }
    output.append(buffer, static_cast<std::size_t>(read));
  }
}

void report_worker_progress(const std::string& output,
                            int& last_progress,
                            const amt::core::ProgressCallback& progress) {
  const auto worker_progress = latest_worker_progress(output);
  if (!worker_progress || *worker_progress <= last_progress) return;
  last_progress = *worker_progress;
  const double fraction = static_cast<double>(*worker_progress) / 100.0;
  amt::core::report_progress(progress, 0.12 + fraction * 0.76);
}

std::optional<std::string> run_worker_process(
    const WorkerSeparationProviderConfig& config,
    const SeparationRequest& request,
    const std::filesystem::path& output_directory,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;

  HANDLE raw_read = nullptr;
  HANDLE raw_write = nullptr;
  if (!CreatePipe(&raw_read, &raw_write, &security, 0U)) {
    error = "unable to create separation worker output pipe";
    return std::nullopt;
  }
  UniqueHandle read_pipe(raw_read);
  UniqueHandle write_pipe(raw_write);
  if (!SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0U)) {
    error = "unable to secure the separation worker output pipe";
    return std::nullopt;
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write_pipe.get();
  startup.hStdError = write_pipe.get();
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION process_info{};
  auto command = worker_command_line(config, request, output_directory, error);
  if (!command) return std::nullopt;
  const auto application = config.worker_executable.wstring();
  const auto working_directory = config.worker_executable.parent_path().wstring();
  const BOOL created = CreateProcessW(
      application.c_str(), command->data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW, nullptr,
      working_directory.empty() ? nullptr : working_directory.c_str(),
      &startup, &process_info);
  if (created == FALSE) {
    error = "unable to launch isolated separation worker";
    return std::nullopt;
  }

  UniqueHandle process(process_info.hProcess);
  UniqueHandle thread(process_info.hThread);
  write_pipe.reset();
  amt::core::report_progress(progress, 0.12);

  std::string worker_output;
  int last_progress = -1;
  const auto started = std::chrono::steady_clock::now();
  while (true) {
    if (!drain_pipe(read_pipe.get(), worker_output,
                    config.maximum_worker_output_bytes, error)) {
      TerminateProcess(process.get(), 120U);
      WaitForSingleObject(process.get(), 5000U);
      return std::nullopt;
    }
    report_worker_progress(worker_output, last_progress, progress);

    if (cancellation != nullptr && cancellation->is_cancelled()) {
      TerminateProcess(process.get(), 121U);
      WaitForSingleObject(process.get(), 5000U);
      error = "source separation cancelled";
      return std::nullopt;
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (elapsed > std::chrono::seconds(config.maximum_runtime_seconds)) {
      TerminateProcess(process.get(), 122U);
      WaitForSingleObject(process.get(), 5000U);
      error = "source separation exceeded the configured worker runtime bound";
      return std::nullopt;
    }

    const DWORD wait = WaitForSingleObject(process.get(), 50U);
    if (wait == WAIT_OBJECT_0) break;
    if (wait == WAIT_FAILED) {
      error = "waiting for the separation worker failed";
      TerminateProcess(process.get(), 123U);
      WaitForSingleObject(process.get(), 5000U);
      return std::nullopt;
    }
  }

  if (!drain_pipe(read_pipe.get(), worker_output,
                  config.maximum_worker_output_bytes, error)) {
    return std::nullopt;
  }
  report_worker_progress(worker_output, last_progress, progress);

  DWORD exit_code = 0U;
  if (!GetExitCodeProcess(process.get(), &exit_code)) {
    error = "unable to obtain separation worker exit status";
    return std::nullopt;
  }
  amt::core::report_progress(progress, 0.90);

  const auto json = final_worker_json(worker_output);
  if (!json) {
    error = "separation worker did not return a valid bounded result envelope";
    return std::nullopt;
  }
  if (exit_code != 0U || json->find("\"ok\":true") == std::string::npos) {
    const auto worker_error = extract_json_string(*json, "error");
    error = worker_error && !worker_error->empty()
        ? *worker_error
        : "isolated separation worker failed";
    return std::nullopt;
  }
  return json;
}

#endif

}  // namespace

WorkerSeparationProvider::WorkerSeparationProvider(
    WorkerSeparationProviderConfig config)
    : config_(std::move(config)) {}

bool WorkerSeparationProvider::available() const noexcept {
#ifdef _WIN32
  if (!valid_contract(config_)) return false;
  std::error_code worker_error;
  const bool worker_exists = std::filesystem::is_regular_file(
      config_.worker_executable, worker_error);
  if (worker_error || !worker_exists) return false;
  std::error_code model_error;
  const bool model_exists = std::filesystem::is_regular_file(
      config_.model_artifact, model_error);
  return !model_error && model_exists;
#else
  return false;
#endif
}

SeparationModelManifest WorkerSeparationProvider::model_manifest() const {
  return config_.manifest;
}

std::optional<SeparationResult> WorkerSeparationProvider::separate(
    const SeparationRequest& request,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (request.source_path.empty()) {
    error = "worker separation request is missing the canonical source path";
    return std::nullopt;
  }
  if (!request.request_stem_audio) {
    error = "worker separation provider requires stem-audio output for source diagnosis";
    return std::nullopt;
  }
  if (request.request_time_frequency_masks) {
    error = "worker separation provider does not yet expose time-frequency mask artifacts";
    return std::nullopt;
  }
  if (cancellation != nullptr && cancellation->is_cancelled()) {
    error = "source separation cancelled";
    return std::nullopt;
  }
  if (!available()) {
    error = "isolated separation worker or configured model package is unavailable";
    return std::nullopt;
  }

#ifndef _WIN32
  (void)progress;
  error = "isolated production separation provider is Windows-first in Phase 5";
  return std::nullopt;
#else
  auto roles = selected_roles(request, config_.manifest, error);
  if (roles.empty()) {
    if (error.empty()) {
      error = "worker separation request selected no usable source roles";
    }
    return std::nullopt;
  }

  amt::core::report_progress(progress, 0.02);
  std::string fingerprint_error;
  const auto fingerprint = amt::core::fingerprint_file_sha256(
      config_.model_artifact, fingerprint_error, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, 0.02 + value * 0.06);
      });
  if (!fingerprint) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "source separation cancelled";
    } else {
      error = "unable to verify separation model artifact: " + fingerprint_error;
    }
    return std::nullopt;
  }
  if (!ascii_equal_ignore_case(fingerprint->sha256,
                               config_.manifest.model_sha256)) {
    error = "separation model artifact SHA-256 does not match the approved manifest";
    return std::nullopt;
  }

  const auto output_directory = request.cache_directory.empty()
      ? fallback_output_directory(config_)
      : request.cache_directory;
  std::error_code directory_error;
  std::filesystem::create_directories(output_directory, directory_error);
  if (directory_error) {
    error = "unable to prepare managed source-estimate directory: " +
            directory_error.message();
    return std::nullopt;
  }

  const auto worker_json = run_worker_process(
      config_, request, output_directory, error, cancellation, progress);
  if (!worker_json) return std::nullopt;

  const auto sample_rate = extract_int64(*worker_json, "sampleRate");
  const auto frames = extract_int64(*worker_json, "frames");
  const auto stem_count = extract_int64(*worker_json, "stemCount");
  if (!sample_rate || !frames || !stem_count ||
      *sample_rate != config_.manifest.expected_input_sample_rate ||
      *frames <= 0 ||
      *stem_count != static_cast<std::int64_t>(config_.manifest.stem_taxonomy.size())) {
    error = "separation worker returned geometry inconsistent with the model contract";
    return std::nullopt;
  }

  amt::codec::SndFileCodecService codecs;
  if (!codecs.available()) {
    error = "unable to validate worker stem outputs because the codec backend is unavailable";
    return std::nullopt;
  }

  SeparationResult result;
  result.model = config_.manifest;
  result.sample_rate = static_cast<int>(*sample_rate);
  result.frames = *frames;
  result.overall_confidence = config_.contract.calibrated_output_confidence;
  result.complete_reconstruction =
      config_.contract.complete_reconstruction &&
      roles.size() == config_.manifest.stem_taxonomy.size();

  for (const auto role : roles) {
    const auto path = output_directory / "stems" / (stem_role_name(role) + ".wav");
    std::string probe_error;
    const auto metadata = codecs.probe(path, probe_error);
    if (!metadata || metadata->sample_rate != result.sample_rate ||
        metadata->frames != result.frames || metadata->channels != 2) {
      error = "worker stem output failed geometry validation for " +
              stem_role_name(role);
      if (!probe_error.empty()) error += ": " + probe_error;
      return std::nullopt;
    }
    result.artifacts.push_back({
        .kind = CacheArtifactKind::stem_audio,
        .role = role,
        .path = path,
        .confidence = result.overall_confidence});
  }

  amt::core::report_progress(progress, 1.0);
  return result;
#endif
}

}  // namespace amt::separation
