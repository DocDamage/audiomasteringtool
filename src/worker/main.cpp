#include <cctype>
#include <iostream>
#include <optional>
#include <string>

#include "amt/core/InferenceBackend.h"
#include "amt/core/Version.h"

namespace {
constexpr int kProtocolVersion = 1;

std::optional<std::string> extract_string(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const auto key_pos = json.find(needle);
  if (key_pos == std::string::npos) return std::nullopt;
  const auto colon = json.find(':', key_pos + needle.size());
  if (colon == std::string::npos) return std::nullopt;
  const auto first_quote = json.find('"', colon + 1);
  if (first_quote == std::string::npos) return std::nullopt;
  const auto second_quote = json.find('"', first_quote + 1);
  if (second_quote == std::string::npos) return std::nullopt;
  return json.substr(first_quote + 1, second_quote - first_quote - 1);
}

std::optional<int> extract_int(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const auto key_pos = json.find(needle);
  if (key_pos == std::string::npos) return std::nullopt;
  const auto colon = json.find(':', key_pos + needle.size());
  if (colon == std::string::npos) return std::nullopt;
  auto pos = colon + 1;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
  bool negative = false;
  if (pos < json.size() && json[pos] == '-') {
    negative = true;
    ++pos;
  }
  if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos]))) return std::nullopt;
  int value = 0;
  while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
    value = value * 10 + (json[pos] - '0');
    ++pos;
  }
  return negative ? -value : value;
}

bool safe_request_id(const std::string& id) {
  if (id.empty() || id.size() > 128) return false;
  for (const unsigned char c : id) {
    if (!(std::isalnum(c) || c == '-' || c == '_' || c == ':' || c == '.')) return false;
  }
  return true;
}

void respond(const std::string& request_id, bool ok, const std::string& payload,
             const std::string& error) {
  std::cout << "{\"protocol\":" << kProtocolVersion << ",\"requestId\":\"" << request_id
            << "\",\"ok\":" << (ok ? "true" : "false") << ",\"payload\":" << payload
            << ",\"error\":";
  if (error.empty()) {
    std::cout << "null";
  } else {
    std::cout << "\"" << error << "\"";
  }
  std::cout << "}\n" << std::flush;
}

int run_stdio() {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.size() > 1024 * 1024) {
      respond("invalid", false, "{}", "message_too_large");
      continue;
    }

    const auto protocol = extract_int(line, "protocol");
    const auto request_id = extract_string(line, "requestId");
    const auto type = extract_string(line, "type");
    if (!protocol || *protocol != kProtocolVersion || !request_id || !safe_request_id(*request_id) ||
        !type) {
      respond(request_id && safe_request_id(*request_id) ? *request_id : "invalid", false, "{}",
              "invalid_envelope");
      continue;
    }

    if (*type == "health") {
      respond(*request_id, true,
              "{\"status\":\"ok\",\"version\":\"" + std::string(amt::core::version()) +
                  "\"}",
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
}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--health") {
    std::cout << "{\"status\":\"ok\",\"version\":\"" << amt::core::version() << "\"}\n";
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--stdio") return run_stdio();

  auto backend = amt::core::make_cpu_inference_backend();
  const auto result = backend->run({.model_id = "phase0-smoke", .input = {1.0F, 2.0F, 3.0F}});
  if (!result.ok) {
    std::cerr << result.error << '\n';
    return 1;
  }

  std::cout << "AudioMasteringTool worker ready (" << amt::core::version() << ")\n";
  return 0;
}
