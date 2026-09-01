#include "amt/separation/ModelRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amt::separation {
namespace {

constexpr std::size_t kMaximumRegistryBytes = 2U * 1024U * 1024U;
constexpr std::size_t kMaximumJsonDepth = 32U;

enum class JsonKind { null_value, boolean, number, string, array, object };

struct JsonValue {
  JsonKind kind{JsonKind::null_value};
  bool boolean{false};
  double number{0.0};
  std::string string;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  std::optional<JsonValue> parse(std::string& error) {
    error.clear();
    auto value = parse_value(0U, error);
    if (!value) return std::nullopt;
    skip_space();
    if (position_ != input_.size()) {
      error = "model registry contains trailing data at byte " +
              std::to_string(position_);
      return std::nullopt;
    }
    return value;
  }

 private:
  void skip_space() {
    while (position_ < input_.size()) {
      const char c = input_[position_];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
      ++position_;
    }
  }

  bool consume(const char expected) {
    skip_space();
    if (position_ >= input_.size() || input_[position_] != expected) return false;
    ++position_;
    return true;
  }

  bool consume_literal(const std::string_view literal) {
    skip_space();
    if (input_.substr(position_, literal.size()) != literal) return false;
    position_ += literal.size();
    return true;
  }

  static int hex_digit(const char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  }

  static bool append_utf8(std::string& output, const std::uint32_t codepoint) {
    if (codepoint <= 0x7FU) {
      output.push_back(static_cast<char>(codepoint));
      return true;
    }
    if (codepoint <= 0x7FFU) {
      output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
      return true;
    }
    if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) return false;
    if (codepoint <= 0xFFFFU) {
      output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
      output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
      return true;
    }
    if (codepoint <= 0x10FFFFU) {
      output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
      output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
      return true;
    }
    return false;
  }

  std::optional<std::string> parse_string(std::string& error) {
    skip_space();
    if (position_ >= input_.size() || input_[position_] != '"') {
      error = "expected JSON string at byte " + std::to_string(position_);
      return std::nullopt;
    }
    ++position_;
    std::string output;
    while (position_ < input_.size()) {
      const unsigned char c = static_cast<unsigned char>(input_[position_++]);
      if (c == '"') return output;
      if (c < 0x20U) {
        error = "control character in JSON string";
        return std::nullopt;
      }
      if (c != '\\') {
        output.push_back(static_cast<char>(c));
        continue;
      }
      if (position_ >= input_.size()) {
        error = "unterminated JSON escape";
        return std::nullopt;
      }
      const char escape = input_[position_++];
      switch (escape) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
          if (position_ + 4U > input_.size()) {
            error = "truncated JSON Unicode escape";
            return std::nullopt;
          }
          std::uint32_t codepoint = 0U;
          for (std::size_t index = 0U; index < 4U; ++index) {
            const int digit = hex_digit(input_[position_ + index]);
            if (digit < 0) {
              error = "invalid JSON Unicode escape";
              return std::nullopt;
            }
            codepoint = (codepoint << 4U) | static_cast<std::uint32_t>(digit);
          }
          position_ += 4U;
          if (!append_utf8(output, codepoint)) {
            error = "unsupported surrogate JSON Unicode escape";
            return std::nullopt;
          }
          break;
        }
        default:
          error = "invalid JSON string escape";
          return std::nullopt;
      }
    }
    error = "unterminated JSON string";
    return std::nullopt;
  }

  std::optional<JsonValue> parse_number(std::string& error) {
    skip_space();
    const std::size_t start = position_;
    if (position_ < input_.size() && input_[position_] == '-') ++position_;
    if (position_ >= input_.size()) return std::nullopt;
    if (input_[position_] == '0') {
      ++position_;
    } else {
      if (input_[position_] < '1' || input_[position_] > '9') return std::nullopt;
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      if (position_ >= input_.size() || input_[position_] < '0' ||
          input_[position_] > '9') {
        error = "invalid JSON number fraction";
        return std::nullopt;
      }
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      if (position_ >= input_.size() || input_[position_] < '0' ||
          input_[position_] > '9') {
        error = "invalid JSON number exponent";
        return std::nullopt;
      }
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    }

    const std::string token(input_.substr(start, position_ - start));
    char* end = nullptr;
    const double parsed = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
      error = "invalid or non-finite JSON number";
      return std::nullopt;
    }
    JsonValue value;
    value.kind = JsonKind::number;
    value.number = parsed;
    return value;
  }

  std::optional<JsonValue> parse_value(const std::size_t depth,
                                       std::string& error) {
    if (depth > kMaximumJsonDepth) {
      error = "model registry JSON nesting is too deep";
      return std::nullopt;
    }
    skip_space();
    if (position_ >= input_.size()) {
      error = "unexpected end of model registry JSON";
      return std::nullopt;
    }

    if (input_[position_] == '"') {
      auto text = parse_string(error);
      if (!text) return std::nullopt;
      JsonValue value;
      value.kind = JsonKind::string;
      value.string = std::move(*text);
      return value;
    }
    if (input_[position_] == '{') return parse_object(depth + 1U, error);
    if (input_[position_] == '[') return parse_array(depth + 1U, error);
    if (consume_literal("true")) {
      JsonValue value;
      value.kind = JsonKind::boolean;
      value.boolean = true;
      return value;
    }
    if (consume_literal("false")) {
      JsonValue value;
      value.kind = JsonKind::boolean;
      value.boolean = false;
      return value;
    }
    if (consume_literal("null")) return JsonValue{};
    auto number = parse_number(error);
    if (number) return number;
    if (error.empty()) {
      error = "invalid JSON value at byte " + std::to_string(position_);
    }
    return std::nullopt;
  }

  std::optional<JsonValue> parse_array(const std::size_t depth,
                                       std::string& error) {
    if (!consume('[')) return std::nullopt;
    JsonValue value;
    value.kind = JsonKind::array;
    skip_space();
    if (consume(']')) return value;
    while (true) {
      auto element = parse_value(depth, error);
      if (!element) return std::nullopt;
      value.array.push_back(std::move(*element));
      skip_space();
      if (consume(']')) return value;
      if (!consume(',')) {
        error = "expected comma in JSON array";
        return std::nullopt;
      }
    }
  }

  std::optional<JsonValue> parse_object(const std::size_t depth,
                                        std::string& error) {
    if (!consume('{')) return std::nullopt;
    JsonValue value;
    value.kind = JsonKind::object;
    skip_space();
    if (consume('}')) return value;
    while (true) {
      auto key = parse_string(error);
      if (!key) return std::nullopt;
      if (!consume(':')) {
        error = "expected colon in JSON object";
        return std::nullopt;
      }
      auto member = parse_value(depth, error);
      if (!member) return std::nullopt;
      if (!value.object.emplace(*key, std::move(*member)).second) {
        error = "duplicate JSON object key: " + *key;
        return std::nullopt;
      }
      skip_space();
      if (consume('}')) return value;
      if (!consume(',')) {
        error = "expected comma in JSON object";
        return std::nullopt;
      }
    }
  }

  std::string_view input_;
  std::size_t position_{0U};
};

const JsonValue* member(const JsonValue& object, const std::string& key) {
  if (object.kind != JsonKind::object) return nullptr;
  const auto iterator = object.object.find(key);
  return iterator == object.object.end() ? nullptr : &iterator->second;
}

std::optional<std::string> string_member(const JsonValue& object,
                                         const std::string& key,
                                         std::string& error,
                                         const bool allow_empty = false) {
  const auto* value = member(object, key);
  if (value == nullptr || value->kind != JsonKind::string ||
      (!allow_empty && value->string.empty())) {
    error = "model registry field '" + key + "' must be a " +
            (allow_empty ? "string" : "non-empty string");
    return std::nullopt;
  }
  return value->string;
}

std::optional<bool> bool_member(const JsonValue& object,
                                const std::string& key,
                                std::string& error) {
  const auto* value = member(object, key);
  if (value == nullptr || value->kind != JsonKind::boolean) {
    error = "model registry field '" + key + "' must be boolean";
    return std::nullopt;
  }
  return value->boolean;
}

std::optional<double> number_member(const JsonValue& object,
                                    const std::string& key,
                                    std::string& error) {
  const auto* value = member(object, key);
  if (value == nullptr || value->kind != JsonKind::number ||
      !std::isfinite(value->number)) {
    error = "model registry field '" + key + "' must be a finite number";
    return std::nullopt;
  }
  return value->number;
}

std::optional<std::uint64_t> integer_member(const JsonValue& object,
                                            const std::string& key,
                                            std::string& error) {
  const auto value = number_member(object, key, error);
  if (!value) return std::nullopt;
  if (*value < 0.0 || std::floor(*value) != *value ||
      *value > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    error = "model registry field '" + key + "' must be a non-negative integer";
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(*value);
}

std::optional<std::vector<std::string>> string_array_member(
    const JsonValue& object,
    const std::string& key,
    std::string& error) {
  const auto* value = member(object, key);
  if (value == nullptr || value->kind != JsonKind::array || value->array.empty()) {
    error = "model registry field '" + key + "' must be a non-empty string array";
    return std::nullopt;
  }
  std::vector<std::string> output;
  output.reserve(value->array.size());
  for (const auto& element : value->array) {
    if (element.kind != JsonKind::string || element.string.empty()) {
      error = "model registry field '" + key + "' contains an invalid string";
      return std::nullopt;
    }
    output.push_back(element.string);
  }
  return output;
}

std::optional<std::string> read_registry(const std::filesystem::path& path,
                                         std::string& error) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    error = "unable to open model registry: " + path.string();
    return std::nullopt;
  }
  const auto size = input.tellg();
  if (size < 0 || static_cast<std::uint64_t>(size) > kMaximumRegistryBytes) {
    error = "model registry exceeds the bounded file size";
    return std::nullopt;
  }
  input.seekg(0, std::ios::beg);
  std::string content(static_cast<std::size_t>(size), '\0');
  if (!content.empty()) input.read(content.data(), size);
  if (!input && !content.empty()) {
    error = "unable to read model registry";
    return std::nullopt;
  }
  return content;
}

std::filesystem::path path_from_utf8(const std::string& value) {
  std::u8string converted;
  converted.reserve(value.size());
  for (const unsigned char byte : value) converted.push_back(static_cast<char8_t>(byte));
  return std::filesystem::path(converted);
}

bool safe_relative_artifact(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& component : path) {
    if (component == "..") return false;
  }
  return true;
}

bool contains_cpu_provider(const std::vector<std::string>& providers) {
  return std::any_of(providers.begin(), providers.end(), [](std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return value == "cpu";
  });
}

std::optional<WorkerSeparationProviderConfig> parse_active_model(
    const JsonValue& model,
    const std::filesystem::path& registry_path,
    const std::filesystem::path& worker_executable,
    const std::filesystem::path& fallback_output_root,
    std::string& error) {
  WorkerSeparationProviderConfig config;

  const auto id = string_member(model, "id", error);
  const auto version = string_member(model, "version", error);
  const auto source = string_member(model, "source", error);
  const auto weight_provenance = string_member(model, "weightProvenance", error);
  const auto artifact_text = string_member(model, "artifact", error);
  const auto sha256 = string_member(model, "sha256", error);
  const auto runtime = string_member(model, "runtime", error);
  const auto code_license = string_member(model, "codeLicense", error);
  const auto weights_license = string_member(model, "weightsLicense", error);
  const auto commercial_reviewed = bool_member(model, "commercialUseReviewed", error);
  const auto commercial_allowed = bool_member(model, "commercialUse", error);
  const auto redistribution_reviewed = bool_member(model, "redistributionReviewed", error);
  const auto redistribution_allowed = bool_member(model, "redistributionAllowed", error);
  const auto attribution = string_member(model, "attribution", error, true);
  const auto benchmark = string_member(model, "benchmarkRecord", error);
  const auto security = string_member(model, "securityReview", error);
  const auto input_sample_rate = integer_member(model, "inputSampleRate", error);
  const auto providers = string_array_member(model, "executionProviders", error);
  const auto stems = string_array_member(model, "stemTaxonomy", error);
  if (!id || !version || !source || !weight_provenance || !artifact_text || !sha256 ||
      !runtime || !code_license || !weights_license || !commercial_reviewed ||
      !commercial_allowed || !redistribution_reviewed || !redistribution_allowed ||
      !attribution || !benchmark || !security || !input_sample_rate || !providers ||
      !stems) {
    return std::nullopt;
  }
  if (*runtime != "onnxruntime-worker-v1") {
    error = "active separation model runtime must be onnxruntime-worker-v1";
    return std::nullopt;
  }
  if (sha256->size() != 64U || !std::all_of(sha256->begin(), sha256->end(),
      [](const unsigned char c) { return std::isxdigit(c) != 0; })) {
    error = "active separation model SHA-256 is invalid";
    return std::nullopt;
  }
  if (*input_sample_rate == 0U ||
      *input_sample_rate > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    error = "active separation model input sample rate is invalid";
    return std::nullopt;
  }
  if (!contains_cpu_provider(*providers)) {
    error = "active separation model must declare a CPU execution-provider fallback";
    return std::nullopt;
  }

  const auto artifact = path_from_utf8(*artifact_text);
  if (!safe_relative_artifact(artifact)) {
    error = "active separation model artifact path must stay inside the models directory";
    return std::nullopt;
  }

  std::vector<StemRole> taxonomy;
  taxonomy.reserve(stems->size());
  for (const auto& name : *stems) {
    const auto role = stem_role_from_name(name);
    if (role == StemRole::unknown) {
      error = "active separation model declares an unknown stem role: " + name;
      return std::nullopt;
    }
    if (std::find(taxonomy.begin(), taxonomy.end(), role) != taxonomy.end()) {
      error = "active separation model stem taxonomy contains a duplicate role: " + name;
      return std::nullopt;
    }
    taxonomy.push_back(role);
  }

  const auto* contract = member(model, "onnxContract");
  if (contract == nullptr || contract->kind != JsonKind::object) {
    error = "active separation model is missing onnxContract";
    return std::nullopt;
  }
  const auto input_tensor = string_member(*contract, "inputTensor", error);
  const auto output_tensor = string_member(*contract, "outputTensor", error);
  const auto chunk_frames = integer_member(*contract, "chunkFrames", error);
  const auto overlap_frames = integer_member(*contract, "overlapFrames", error);
  const auto confidence = number_member(*contract, "calibratedOutputConfidence", error);
  const auto complete_reconstruction = bool_member(*contract, "completeReconstruction", error);
  if (!input_tensor || !output_tensor || !chunk_frames || !overlap_frames ||
      !confidence || !complete_reconstruction) {
    return std::nullopt;
  }
  if (*chunk_frames > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
      *overlap_frames > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
      *confidence <= 0.0 || *confidence > 1.0) {
    error = "active separation model ONNX contract contains invalid bounds/confidence";
    return std::nullopt;
  }

  config.worker_executable = worker_executable;
  config.model_artifact = registry_path.parent_path() / artifact;
  config.fallback_output_root = fallback_output_root;
  config.execution_provider = "cpu";
  config.manifest.model_name = *id;
  config.manifest.model_version = *version;
  config.manifest.model_sha256 = *sha256;
  config.manifest.architecture_source = *source;
  config.manifest.weight_provenance = *weight_provenance;
  config.manifest.code_license = *code_license;
  config.manifest.weights_license = *weights_license;
  config.manifest.redistribution_reviewed = *redistribution_reviewed;
  config.manifest.redistribution_allowed = *redistribution_allowed;
  config.manifest.commercial_use_reviewed = *commercial_reviewed;
  config.manifest.commercial_use_allowed = *commercial_allowed;
  config.manifest.attribution_requirements = *attribution;
  config.manifest.supported_execution_providers = *providers;
  config.manifest.expected_input_sample_rate = static_cast<int>(*input_sample_rate);
  config.manifest.stem_taxonomy = std::move(taxonomy);
  config.manifest.benchmark_record = *benchmark;
  config.manifest.security_reviewed = *security == "approved";
  config.contract.input_tensor_name = *input_tensor;
  config.contract.output_tensor_name = *output_tensor;
  config.contract.chunk_frames = static_cast<std::size_t>(*chunk_frames);
  config.contract.overlap_frames = static_cast<std::size_t>(*overlap_frames);
  config.contract.calibrated_output_confidence = *confidence;
  config.contract.complete_reconstruction = *complete_reconstruction;
  return config;
}

}  // namespace

std::optional<ModelRegistrySelection> load_model_registry_selection(
    const std::filesystem::path& registry_path,
    const std::filesystem::path& worker_executable,
    std::string& error,
    const std::filesystem::path& fallback_output_root) {
  error.clear();
  const auto text = read_registry(registry_path, error);
  if (!text) return std::nullopt;

  JsonParser parser(*text);
  const auto root = parser.parse(error);
  if (!root) return std::nullopt;
  if (root->kind != JsonKind::object) {
    error = "model registry root must be a JSON object";
    return std::nullopt;
  }

  const auto schema = integer_member(*root, "schemaVersion", error);
  if (!schema || *schema != 2U) {
    if (error.empty()) error = "unsupported model registry schema; expected schemaVersion 2";
    return std::nullopt;
  }

  ModelRegistrySelection selection;
  const auto* active = member(*root, "activeSeparationModel");
  if (active == nullptr || active->kind == JsonKind::null_value) return selection;
  if (active->kind != JsonKind::string || active->string.empty()) {
    error = "activeSeparationModel must be null or a non-empty model id";
    return std::nullopt;
  }

  const auto* models = member(*root, "models");
  if (models == nullptr || models->kind != JsonKind::array) {
    error = "model registry models field must be an array";
    return std::nullopt;
  }

  const JsonValue* selected = nullptr;
  for (const auto& model : models->array) {
    if (model.kind != JsonKind::object) {
      error = "model registry models array contains a non-object entry";
      return std::nullopt;
    }
    const auto* id = member(model, "id");
    if (id != nullptr && id->kind == JsonKind::string && id->string == active->string) {
      if (selected != nullptr) {
        error = "active separation model id appears more than once in the registry";
        return std::nullopt;
      }
      selected = &model;
    }
  }
  if (selected == nullptr) {
    error = "activeSeparationModel does not match a model registry entry";
    return std::nullopt;
  }

  auto config = parse_active_model(*selected, registry_path, worker_executable,
                                   fallback_output_root, error);
  if (!config) return std::nullopt;
  selection.active_separation_model = std::move(*config);

  const auto eligibility = evaluate_model_for_bundled_production(
      selection.active_separation_model->manifest);
  if (!eligibility.eligible_for_bundled_production) {
    selection.warnings.emplace_back(
        "active separation model is configured but is not eligible for bundled production use");
    selection.warnings.insert(selection.warnings.end(), eligibility.blockers.begin(),
                              eligibility.blockers.end());
  }
  if (!std::filesystem::exists(selection.active_separation_model->model_artifact)) {
    selection.warnings.emplace_back(
        "active separation model artifact is not present in the packaged models directory");
  }
  if (!std::filesystem::exists(worker_executable)) {
    selection.warnings.emplace_back(
        "isolated separation worker executable is not present in the application package");
  }
  return selection;
}

}  // namespace amt::separation
