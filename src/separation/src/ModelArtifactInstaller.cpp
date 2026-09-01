#include "amt/separation/ModelArtifactInstaller.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "amt/core/FileFingerprint.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace amt::separation {
namespace {

struct TrustedModelArtifact {
  std::string_view model_name;
  std::string_view model_version;
  std::string_view sha256;
  std::uint64_t size_bytes{0U};
  std::string_view url;
};

constexpr TrustedModelArtifact kTrustedModels[] = {
    {.model_name = "htdemucs-onnx-fp16weights",
     .model_version = "d54ed9eb60e258ea82131c6ee14578628816456a",
     .sha256 = "d05c269db7e4e50474ed9fa5759fad70b8063887c7158be0a7d8fc1adcfdb70a",
     .size_bytes = 173546540U,
     .url = "https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/d54ed9eb60e258ea82131c6ee14578628816456a/htdemucs_fp16weights.onnx?download=true"},
};

[[nodiscard]] bool cancelled(const amt::core::CancellationToken* cancellation) noexcept {
  return cancellation != nullptr && cancellation->is_cancelled();
}

[[nodiscard]] char ascii_lower(const char value) noexcept {
  return value >= 'A' && value <= 'Z'
      ? static_cast<char>(value - 'A' + 'a')
      : value;
}

[[nodiscard]] bool ascii_equal_ignore_case(const std::string_view first,
                                           const std::string_view second) noexcept {
  if (first.size() != second.size()) return false;
  for (std::size_t index = 0U; index < first.size(); ++index) {
    if (ascii_lower(first[index]) != ascii_lower(second[index])) return false;
  }
  return true;
}

[[nodiscard]] const TrustedModelArtifact* trusted_artifact(
    const WorkerSeparationProviderConfig& config) noexcept {
  for (const auto& candidate : kTrustedModels) {
    if (candidate.model_name == config.manifest.model_name &&
        candidate.model_version == config.manifest.model_version &&
        ascii_equal_ignore_case(candidate.sha256, config.manifest.model_sha256)) {
      return &candidate;
    }
  }
  return nullptr;
}

bool remove_file_if_present(const std::filesystem::path& path,
                            std::string& error) {
  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
  if (remove_error) {
    error = "unable to remove stale model artifact sidecar: " +
            remove_error.message();
    return false;
  }
  return true;
}

bool verify_existing_artifact(const WorkerSeparationProviderConfig& config,
                              bool& valid,
                              std::string& error,
                              const amt::core::CancellationToken* cancellation,
                              const amt::core::ProgressCallback& progress) {
  valid = false;
  std::error_code exists_error;
  if (!std::filesystem::is_regular_file(config.model_artifact, exists_error)) {
    if (exists_error) {
      error = "unable to inspect configured model artifact: " +
              exists_error.message();
      return false;
    }
    return true;
  }

  std::string fingerprint_error;
  const auto fingerprint = amt::core::fingerprint_file_sha256(
      config.model_artifact, fingerprint_error, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, value);
      });
  if (!fingerprint) {
    if (cancelled(cancellation)) {
      error = "model installation cancelled";
    } else {
      error = "unable to verify installed model artifact: " + fingerprint_error;
    }
    return false;
  }
  valid = ascii_equal_ignore_case(fingerprint->sha256,
                                  config.manifest.model_sha256);
  return true;
}

#ifdef _WIN32

struct InternetHandle {
  HINTERNET value{nullptr};
  InternetHandle() = default;
  explicit InternetHandle(HINTERNET handle) : value(handle) {}
  InternetHandle(const InternetHandle&) = delete;
  InternetHandle& operator=(const InternetHandle&) = delete;
  InternetHandle(InternetHandle&& other) noexcept : value(other.value) {
    other.value = nullptr;
  }
  InternetHandle& operator=(InternetHandle&& other) noexcept {
    if (this == &other) return *this;
    reset();
    value = other.value;
    other.value = nullptr;
    return *this;
  }
  ~InternetHandle() { reset(); }
  void reset(HINTERNET handle = nullptr) noexcept {
    if (value != nullptr) WinHttpCloseHandle(value);
    value = handle;
  }
  [[nodiscard]] HINTERNET get() const noexcept { return value; }
  explicit operator bool() const noexcept { return value != nullptr; }
};

[[nodiscard]] std::wstring wide_utf8(const std::string_view text) {
  if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return {};
  }
  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0);
  if (required <= 0) return {};
  std::wstring output(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), output.data(), required) != required) {
    return {};
  }
  return output;
}

bool query_status_code(HINTERNET request, DWORD& status_code) {
  DWORD size = sizeof(status_code);
  return WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &status_code,
                             &size,
                             WINHTTP_NO_HEADER_INDEX) != FALSE;
}

std::optional<std::uint64_t> query_content_length(HINTERNET request) {
  wchar_t buffer[64]{};
  DWORD size = sizeof(buffer);
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                          WINHTTP_HEADER_NAME_BY_INDEX, buffer, &size,
                          WINHTTP_NO_HEADER_INDEX) == FALSE) {
    return std::nullopt;
  }
  try {
    return std::stoull(buffer);
  } catch (...) {
    return std::nullopt;
  }
}

bool download_trusted_artifact(const TrustedModelArtifact& trusted,
                               const std::filesystem::path& partial_path,
                               std::string& error,
                               const amt::core::CancellationToken* cancellation,
                               const amt::core::ProgressCallback& progress) {
  const auto url = wide_utf8(trusted.url);
  if (url.empty()) {
    error = "trusted model download URL is invalid UTF-8";
    return false;
  }

  URL_COMPONENTSW components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0U,
                      &components) == FALSE ||
      components.nScheme != INTERNET_SCHEME_HTTPS ||
      components.dwHostNameLength == 0U || components.dwUrlPathLength == 0U) {
    error = "trusted model download URL could not be parsed as HTTPS";
    return false;
  }

  const std::wstring host(components.lpszHostName, components.dwHostNameLength);
  std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.dwExtraInfoLength > 0U) {
    path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }

  InternetHandle session(WinHttpOpen(
      L"AudioMasteringTool/0.5 model-installer",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0U));
  if (!session) {
    error = "unable to initialize Windows HTTP for model installation";
    return false;
  }

  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(session.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                   &redirect_policy, sizeof(redirect_policy));

  InternetHandle connection(WinHttpConnect(
      session.get(), host.c_str(), components.nPort, 0U));
  if (!connection) {
    error = "unable to connect to trusted model host";
    return false;
  }

  InternetHandle request(WinHttpOpenRequest(
      connection.get(), L"GET", path.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      WINHTTP_FLAG_SECURE));
  if (!request) {
    error = "unable to create trusted model HTTP request";
    return false;
  }

  DWORD redirect_limit = 8U;
  WinHttpSetOption(request.get(), WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
                   &redirect_limit, sizeof(redirect_limit));

  if (WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0U,
                         WINHTTP_NO_REQUEST_DATA, 0U, 0U, 0U) == FALSE ||
      WinHttpReceiveResponse(request.get(), nullptr) == FALSE) {
    error = "trusted model download request failed";
    return false;
  }

  DWORD status_code = 0U;
  if (!query_status_code(request.get(), status_code) || status_code != 200U) {
    error = "trusted model host returned HTTP status " + std::to_string(status_code);
    return false;
  }

  if (const auto content_length = query_content_length(request.get())) {
    if (*content_length != trusted.size_bytes) {
      error = "trusted model download content length does not match the pinned artifact size";
      return false;
    }
  }

  std::ofstream output(partial_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "unable to create model download sidecar";
    return false;
  }

  std::array<char, 64U * 1024U> buffer{};
  std::uint64_t total = 0U;
  while (true) {
    if (cancelled(cancellation)) {
      error = "model installation cancelled";
      output.close();
      return false;
    }

    DWORD available = 0U;
    if (WinHttpQueryDataAvailable(request.get(), &available) == FALSE) {
      error = "trusted model download failed while checking available data";
      output.close();
      return false;
    }
    if (available == 0U) break;

    while (available > 0U) {
      if (cancelled(cancellation)) {
        error = "model installation cancelled";
        output.close();
        return false;
      }
      const DWORD wanted = static_cast<DWORD>(
          std::min<std::size_t>(buffer.size(), static_cast<std::size_t>(available)));
      DWORD read = 0U;
      if (WinHttpReadData(request.get(), buffer.data(), wanted, &read) == FALSE) {
        error = "trusted model download failed while reading data";
        output.close();
        return false;
      }
      if (read == 0U) break;

      total += static_cast<std::uint64_t>(read);
      if (total > trusted.size_bytes) {
        error = "trusted model download exceeded the pinned artifact size";
        output.close();
        return false;
      }
      output.write(buffer.data(), static_cast<std::streamsize>(read));
      if (!output) {
        error = "unable to write model download sidecar";
        output.close();
        return false;
      }
      available -= read;
      amt::core::report_progress(
          progress,
          0.90 * static_cast<double>(total) /
              static_cast<double>(trusted.size_bytes));
    }
  }

  output.flush();
  if (!output) {
    error = "unable to finalize model download sidecar";
    output.close();
    return false;
  }
  output.close();

  if (total != trusted.size_bytes) {
    error = "trusted model download size does not match the pinned artifact size";
    return false;
  }
  return true;
}

#endif

}  // namespace

std::optional<ModelArtifactInstallResult> ensure_model_artifact_installed(
    const WorkerSeparationProviderConfig& config,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  amt::core::report_progress(progress, 0.0);

  const auto* trusted = trusted_artifact(config);
  if (trusted == nullptr) {
    error = "active separation model is not present in AudioMasteringTool's trusted download catalog";
    return std::nullopt;
  }
  if (cancelled(cancellation)) {
    error = "model installation cancelled";
    return std::nullopt;
  }

  bool existing_valid = false;
  if (!verify_existing_artifact(
          config, existing_valid, error, cancellation,
          [&](const double value) {
            amt::core::report_progress(progress, value * 0.15);
          })) {
    return std::nullopt;
  }
  if (existing_valid) {
    amt::core::report_progress(progress, 1.0);
    return ModelArtifactInstallResult{.already_present = true, .downloaded = false};
  }

  std::error_code directory_error;
  std::filesystem::create_directories(config.model_artifact.parent_path(),
                                      directory_error);
  if (directory_error) {
    error = "unable to create model artifact directory: " +
            directory_error.message();
    return std::nullopt;
  }

  if (std::filesystem::exists(config.model_artifact)) {
    std::error_code remove_error;
    std::filesystem::remove(config.model_artifact, remove_error);
    if (remove_error) {
      error = "installed model artifact failed checksum verification and could not be removed: " +
              remove_error.message();
      return std::nullopt;
    }
  }

  auto partial_path = config.model_artifact;
  partial_path += ".download";
  if (!remove_file_if_present(partial_path, error)) return std::nullopt;

#ifndef _WIN32
  error = "trusted first-run model installation is Windows-first in Phase 5";
  return std::nullopt;
#else
  if (!download_trusted_artifact(
          *trusted, partial_path, error, cancellation,
          [&](const double value) {
            amt::core::report_progress(progress, 0.15 + value * 0.70);
          })) {
    std::string ignored;
    remove_file_if_present(partial_path, ignored);
    return std::nullopt;
  }

  std::string fingerprint_error;
  const auto fingerprint = amt::core::fingerprint_file_sha256(
      partial_path, fingerprint_error, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, 0.85 + value * 0.14);
      });
  if (!fingerprint) {
    std::string ignored;
    remove_file_if_present(partial_path, ignored);
    error = cancelled(cancellation)
        ? "model installation cancelled"
        : "downloaded model artifact could not be verified: " + fingerprint_error;
    return std::nullopt;
  }
  if (!ascii_equal_ignore_case(fingerprint->sha256, trusted->sha256)) {
    std::string ignored;
    remove_file_if_present(partial_path, ignored);
    error = "downloaded model artifact failed pinned SHA-256 verification";
    return std::nullopt;
  }

  std::error_code rename_error;
  std::filesystem::rename(partial_path, config.model_artifact, rename_error);
  if (rename_error) {
    std::string ignored;
    remove_file_if_present(partial_path, ignored);
    error = "verified model artifact could not be published atomically: " +
            rename_error.message();
    return std::nullopt;
  }

  amt::core::report_progress(progress, 1.0);
  return ModelArtifactInstallResult{.already_present = false, .downloaded = true};
#endif
}

}  // namespace amt::separation
