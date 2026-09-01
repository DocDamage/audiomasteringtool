#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "OnnxSeparationWorker.h"
#include "amt/core/InferenceBackend.h"
#include "amt/core/Version.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
constexpr int kProtocolVersion = 1;

std::string json_escape(const std::string& text) {
  std::ostringstream output;
  for (const unsigned char c : text) {
    switch (c) {
      case '\\': output << "\\\\"; break;
      case '"': output << "\\\""; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (c < 0x20U) output << "?";
        else output << static_cast<char>(c);
        break;
    }
  }
  return output.str();
}

std::optional<std::string> extract_string(const std::string& json,
                                          const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const auto key_pos = json.find(needle);
  if (key_pos == std::string::npos) return std::nullopt;
  const auto colon = json.find(':', key_pos + needle.size());
  if (colon == std::string::npos) return std::nullopt;
  const auto first_quote = json.find('"', colon + 1U);
  if (first_quote == std::string::npos) return std::nullopt;
  const auto second_quote = json.find('"', first_quote + 1U);
  if (second_quote == std::string::npos) return std::nullopt;
  return json.substr(first_quote + 1U, second_quote - first_quote - 1U);
}

std::optional<int> extract_int(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const auto key_pos = json.find(needle);
  if (key_pos == std::string::npos) return std::nullopt;
  const auto colon = json.find(':', key_pos + needle.size());
  if (colon == std::string::npos) return std::nullopt;
  auto pos = colon + 1U;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
  bool negative = false;
  if (pos < json.size() && json[pos] == '-') {
    negative = true;
    ++pos;
  }
  if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos]))) {
    return std::nullopt;
  }
  int value = 0;
  while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
    value = value * 10 + (json[pos] - '0');
    ++pos;
  }
  return negative ? -value : value;
}

bool safe_request_id(const std::string& id) {
  if (id.empty() || id.size() > 128U) return false;
  for (const unsigned char c : id) {
    if (!(std::isalnum(c) || c == '-' || c == '_' || c == ':' || c == '.')) return false;
  }
  return true;
}

void respond(const std::string& request_id,
             const bool ok,
             const std::string& payload,
             const std::string& error) {
  std::cout << "{\"protocol\":" << kProtocolVersion
            << ",\"requestId\":\"" << json_escape(request_id)
            << "\",\"ok\":" << (ok ? "true" : "false")
            << ",\"payload\":" << payload << ",\"error\":";
  if (error.empty()) std::cout << "null";
  else std::cout << "\"" << json_escape(error) << "\"";
  std::cout << "}\n" << std::flush;
}

int run_stdio() {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.size() > 1024U * 1024U) {
      respond("invalid", false, "{}", "message_too_large");
      continue;
    }

    const auto protocol = extract_int(line, "protocol");
    const auto request_id = extract_string(line, "requestId");
    const auto type = extract_string(line, "type");
    if (!protocol || *protocol != kProtocolVersion || !request_id ||
        !safe_request_id(*request_id) || !type) {
      respond(request_id && safe_request_id(*request_id) ? *request_id : "invalid",
              false, "{}", "invalid_envelope");
      continue;
    }

    if (*type == "health") {
      respond(*request_id, true,
              "{\"status\":\"ok\",\"version\":\"" +
                  json_escape(std::string(amt::core::version())) +
                  "\",\"onnxSeparation\":" +
                  (amt::worker::onnx_separation_compiled() ? "true" : "false") + "}",
              "");
      continue;
    }
    if (*type == "shutdown") {
      respond(*request_id, true, "{\"status\":\"shutting_down\"}", "");
      return 0;
    }
    if (*type == "cancel") {
      respond(*request_id, true, "{\"status\":\"not_running\"}", "");
      continue;
    }

    respond(*request_id, false, "{}", "unsupported_type");
  }
  return 0;
}

std::optional<std::string> argument_value(const std::vector<std::string>& args,
                                          const std::string& key) {
  for (std::size_t index = 0U; index + 1U < args.size(); ++index) {
    if (args[index] == key) return args[index + 1U];
  }
  return std::nullopt;
}

std::optional<std::size_t> parse_size(const std::string& text) {
  if (text.empty()) return std::nullopt;
  std::size_t value = 0U;
  for (const unsigned char c : text) {
    if (!std::isdigit(c)) return std::nullopt;
    const std::size_t digit = static_cast<std::size_t>(c - '0');
    if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
      return std::nullopt;
    }
    value = value * 10U + digit;
  }
  return value;
}

std::vector<std::string> split_csv(const std::string& text) {
  std::vector<std::string> values;
  std::size_t start = 0U;
  while (start <= text.size()) {
    const auto comma = text.find(',', start);
    const auto end = comma == std::string::npos ? text.size() : comma;
    if (end > start) values.push_back(text.substr(start, end - start));
    if (comma == std::string::npos) break;
    start = comma + 1U;
  }
  return values;
}

std::filesystem::path utf8_path(const std::string& text) {
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t*>(text.data()),
      reinterpret_cast<const char8_t*>(text.data() + text.size())));
}

int run_separate_onnx(const std::vector<std::string>& args) {
  const auto model = argument_value(args, "--model");
  const auto source = argument_value(args, "--source");
  const auto output = argument_value(args, "--output");
  const auto sample_rate_text = argument_value(args, "--sample-rate");
  const auto stems_text = argument_value(args, "--stems");
  if (!model || !source || !output || !sample_rate_text || !stems_text) {
    std::cout << "{\"ok\":false,\"error\":\"missing_required_argument\"}\n";
    return 2;
  }

  const auto sample_rate_size = parse_size(*sample_rate_text);
  if (!sample_rate_size || *sample_rate_size == 0U ||
      *sample_rate_size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    std::cout << "{\"ok\":false,\"error\":\"invalid_sample_rate\"}\n";
    return 2;
  }

  amt::worker::OnnxSeparationWorkerRequest request;
  request.model_path = utf8_path(*model);
  request.source_path = utf8_path(*source);
  request.output_directory = utf8_path(*output);
  request.input_sample_rate = static_cast<int>(*sample_rate_size);
  request.stem_names = split_csv(*stems_text);
  if (const auto provider = argument_value(args, "--provider")) {
    request.execution_provider = *provider;
  }
  if (const auto input_name = argument_value(args, "--input-tensor")) {
    request.input_tensor_name = *input_name;
  }
  if (const auto output_name = argument_value(args, "--output-tensor")) {
    request.output_tensor_name = *output_name;
  }
  if (const auto chunk = argument_value(args, "--chunk-frames")) {
    const auto parsed = parse_size(*chunk);
    if (!parsed) {
      std::cout << "{\"ok\":false,\"error\":\"invalid_chunk_frames\"}\n";
      return 2;
    }
    request.chunk_frames = *parsed;
  }
  if (const auto overlap = argument_value(args, "--overlap-frames")) {
    const auto parsed = parse_size(*overlap);
    if (!parsed) {
      std::cout << "{\"ok\":false,\"error\":\"invalid_overlap_frames\"}\n";
      return 2;
    }
    request.overlap_frames = *parsed;
  }

  int last_progress = -1;
  std::string error;
  const auto result = amt::worker::run_onnx_separation(
      request, error, [&last_progress](const double value) {
        int progress = static_cast<int>(value * 100.0 + 0.5);
        if (progress < 0) progress = 0;
        if (progress > 100) progress = 100;
        if (progress == last_progress) return;
        last_progress = progress;
        std::cout << "{\"progress\":" << progress << "}\n" << std::flush;
      });
  if (!result) {
    std::cout << "{\"ok\":false,\"error\":\"" << json_escape(error)
              << "\"}\n";
    return 3;
  }

  std::cout << "{\"ok\":true,\"sampleRate\":" << result->sample_rate
            << ",\"frames\":" << result->frames
            << ",\"stemCount\":" << result->stem_paths.size() << "}\n";
  return 0;
}

int run_main(const std::vector<std::string>& args) {
  if (args.size() > 1U && args[1] == "--health") {
    std::cout << "{\"status\":\"ok\",\"version\":\"" << amt::core::version()
              << "\",\"onnxSeparation\":"
              << (amt::worker::onnx_separation_compiled() ? "true" : "false")
              << "}\n";
    return 0;
  }
  if (args.size() > 1U && args[1] == "--stdio") return run_stdio();
  if (args.size() > 1U && args[1] == "--separate-onnx") return run_separate_onnx(args);

  auto backend = amt::core::make_cpu_inference_backend();
  const auto result = backend->run(
      {.model_id = "phase0-smoke", .input = {1.0F, 2.0F, 3.0F}});
  if (!result.ok) {
    std::cerr << result.error << '\n';
    return 1;
  }

  std::cout << "AudioMasteringTool worker ready (" << amt::core::version() << ")\n";
  return 0;
}

#ifdef _WIN32
std::string utf8_from_wide(const wchar_t* value) {
  if (value == nullptr || *value == L'\0') return {};
  const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0,
                                            nullptr, nullptr);
  if (required <= 1) return {};
  std::string output(static_cast<std::size_t>(required), '\0');
  const int written = WideCharToMultiByte(CP_UTF8, 0, value, -1, output.data(),
                                           required, nullptr, nullptr);
  if (written <= 1) return {};
  output.resize(static_cast<std::size_t>(written - 1));
  return output;
}
#endif

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) args.push_back(utf8_from_wide(argv[index]));
  return run_main(args);
}
#else
int main(int argc, char** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) args.emplace_back(argv[index]);
  return run_main(args);
}
#endif
