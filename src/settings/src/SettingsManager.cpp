#include "amt/settings/SettingsManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <limits>
#include <sstream>
#include <system_error>
#include <vector>

namespace amt::settings {

namespace {

std::string escape_json_string(const std::string& s) {
  std::ostringstream o;
  for (auto c : s) {
    switch (c) {
      case '"': o << "\\\""; break;
      case '\\': o << "\\\\"; break;
      case '\b': o << "\\b"; break;
      case '\f': o << "\\f"; break;
      case '\n': o << "\\n"; break;
      case '\r': o << "\\r"; break;
      case '\t': o << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          o << buf;
        } else {
          o << c;
        }
    }
  }
  return o.str();
}

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Object, Array } type{Type::Null};
  bool bool_val{false};
  double num_val{0.0};
  std::string str_val;
  std::map<std::string, JsonValue> obj_val;
  std::vector<JsonValue> arr_val;
};

class SimpleJsonParser {
 public:
  explicit SimpleJsonParser(std::string_view input) : input_(input), idx_(0) {}

  bool parse(JsonValue& out, std::string& error) {
    skip_whitespace();
    if (idx_ >= input_.size()) {
      error = "Empty JSON input";
      return false;
    }
    if (!parse_value(out, error)) {
      return false;
    }
    skip_whitespace();
    if (idx_ < input_.size()) {
      error = "Unexpected trailing characters after JSON document";
      return false;
    }
    return true;
  }

 private:
  std::string_view input_;
  std::size_t idx_{0};

  void skip_whitespace() {
    while (idx_ < input_.size() && (std::isspace(static_cast<unsigned char>(input_[idx_])) || input_[idx_] == '\0')) {
      ++idx_;
    }
  }

  char peek() const {
    return idx_ < input_.size() ? input_[idx_] : '\0';
  }

  char get() {
    return idx_ < input_.size() ? input_[idx_++] : '\0';
  }

  bool parse_value(JsonValue& val, std::string& error) {
    skip_whitespace();
    char c = peek();
    if (c == '{') return parse_object(val, error);
    if (c == '[') return parse_array(val, error);
    if (c == '"') return parse_string(val, error);
    if (c == 't' || c == 'f') return parse_bool(val, error);
    if (c == 'n') return parse_null(val, error);
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number(val, error);

    error = std::string("Unexpected character: ") + (c ? std::string(1, c) : std::string("EOF"));
    return false;
  }

  bool parse_object(JsonValue& val, std::string& error) {
    if (get() != '{') {
      error = "Expected '{'";
      return false;
    }
    val.type = JsonValue::Type::Object;
    val.obj_val.clear();

    skip_whitespace();
    if (peek() == '}') {
      get();
      return true;
    }

    while (idx_ < input_.size()) {
      skip_whitespace();
      if (peek() != '"') {
        error = "Expected string key in object";
        return false;
      }
      JsonValue key_val;
      if (!parse_string(key_val, error)) return false;

      skip_whitespace();
      if (get() != ':') {
        error = "Expected ':' after key";
        return false;
      }

      JsonValue member_val;
      if (!parse_value(member_val, error)) return false;

      if (val.obj_val.contains(key_val.str_val)) {
        error = "Duplicate key in settings JSON: " + key_val.str_val;
        return false;
      }
      val.obj_val.emplace(key_val.str_val, std::move(member_val));

      skip_whitespace();
      char next_c = get();
      if (next_c == '}') return true;
      if (next_c != ',') {
        error = "Expected ',' or '}' in object";
        return false;
      }
    }
    error = "Unterminated object";
    return false;
  }

  bool parse_array(JsonValue& val, std::string& error) {
    if (get() != '[') {
      error = "Expected '['";
      return false;
    }
    val.type = JsonValue::Type::Array;
    val.arr_val.clear();

    skip_whitespace();
    if (peek() == ']') {
      get();
      return true;
    }

    while (idx_ < input_.size()) {
      JsonValue elem;
      if (!parse_value(elem, error)) return false;
      val.arr_val.push_back(std::move(elem));

      skip_whitespace();
      char next_c = get();
      if (next_c == ']') return true;
      if (next_c != ',') {
        error = "Expected ',' or ']' in array";
        return false;
      }
    }
    error = "Unterminated array";
    return false;
  }

  bool parse_string(JsonValue& val, std::string& error) {
    if (get() != '"') {
      error = "Expected '\"'";
      return false;
    }
    val.type = JsonValue::Type::String;
    val.str_val.clear();

    while (idx_ < input_.size()) {
      char c = get();
      if (c == '"') {
        return true;
      }
      if (c == '\\') {
        if (idx_ >= input_.size()) {
          error = "Unfinished escape sequence";
          return false;
        }
        char esc = get();
        switch (esc) {
          case '"': val.str_val.push_back('"'); break;
          case '\\': val.str_val.push_back('\\'); break;
          case '/': val.str_val.push_back('/'); break;
          case 'b': val.str_val.push_back('\b'); break;
          case 'f': val.str_val.push_back('\f'); break;
          case 'n': val.str_val.push_back('\n'); break;
          case 'r': val.str_val.push_back('\r'); break;
          case 't': val.str_val.push_back('\t'); break;
          case 'u': {
            if (idx_ + 4 > input_.size()) {
              error = "Invalid unicode escape";
              return false;
            }
            std::string hex_str(input_.substr(idx_, 4));
            idx_ += 4;
            unsigned int code = 0;
            try {
              std::size_t consumed = 0U;
              code = static_cast<unsigned int>(
                  std::stoul(hex_str, &consumed, 16));
              if (consumed != hex_str.size()) {
                error = "Invalid unicode hex in escape sequence";
                return false;
              }
            } catch (...) {
              error = "Invalid unicode hex in escape sequence";
              return false;
            }
            if (code <= 0x7F) {
              val.str_val.push_back(static_cast<char>(code));
            } else {
              val.str_val.push_back('?');
            }
            break;
          }
          default:
            error = "Invalid JSON escape sequence";
            return false;
        }
      } else {
        if (static_cast<unsigned char>(c) < 0x20U) {
          error = "Unescaped control character in JSON string";
          return false;
        }
        val.str_val.push_back(c);
      }
    }
    error = "Unterminated string";
    return false;
  }

  bool parse_bool(JsonValue& val, std::string& error) {
    if (input_.substr(idx_, 4) == "true") {
      idx_ += 4;
      val.type = JsonValue::Type::Bool;
      val.bool_val = true;
      return true;
    }
    if (input_.substr(idx_, 5) == "false") {
      idx_ += 5;
      val.type = JsonValue::Type::Bool;
      val.bool_val = false;
      return true;
    }
    error = "Invalid boolean literal";
    return false;
  }

  bool parse_null(JsonValue& val, std::string& error) {
    if (input_.substr(idx_, 4) == "null") {
      idx_ += 4;
      val.type = JsonValue::Type::Null;
      return true;
    }
    error = "Invalid null literal";
    return false;
  }

  bool parse_number(JsonValue& val, std::string& error) {
    std::size_t start = idx_;
    if (peek() == '-') ++idx_;
    while (idx_ < input_.size() && (std::isdigit(static_cast<unsigned char>(input_[idx_])) ||
                                   input_[idx_] == '.' || input_[idx_] == 'e' || input_[idx_] == 'E' ||
                                   input_[idx_] == '+' || input_[idx_] == '-')) {
      ++idx_;
    }
    std::string num_str(input_.substr(start, idx_ - start));
    try {
      std::size_t consumed = 0U;
      const double parsed = std::stod(num_str, &consumed);
      if (consumed != num_str.size() || !std::isfinite(parsed)) {
        error = "Invalid number format: " + num_str;
        return false;
      }
      val.type = JsonValue::Type::Number;
      val.num_val = parsed;
      return true;
    } catch (...) {
      error = "Invalid number format: " + num_str;
      return false;
    }
  }
};

bool validate_settings(const AppSettings& settings, std::string& error) {
  if (settings.schema_version != 1) {
    error = "Unsupported settings schema version";
    return false;
  }
  if (settings.buffer_size_frames < 64 || settings.buffer_size_frames > 8192) {
    error = "Audio buffer size must be between 64 and 8192 frames";
    return false;
  }
  if (settings.sample_rate_hz < 8000 || settings.sample_rate_hz > 384000) {
    error = "Sample rate must be between 8000 and 384000 Hz";
    return false;
  }
  if (settings.max_cache_size_mb < 64U ||
      settings.max_cache_size_mb > 1024U * 1024U) {
    error = "Cache budget must be between 64 MB and 1 TB";
    return false;
  }
  if (settings.audio_output_device.size() > 1024U ||
      settings.default_export_recipe.empty() ||
      settings.default_export_recipe.size() > 128U ||
      settings.active_preference_profile.empty() ||
      settings.active_preference_profile.size() > 128U) {
    error = "A persisted settings string is empty or exceeds its allowed length";
    return false;
  }
  return true;
}

}  // namespace

std::filesystem::path SettingsManager::default_settings_path() {
#ifdef _WIN32
  const char* local_app_data = std::getenv("LOCALAPPDATA");
  if (local_app_data && *local_app_data) {
    return std::filesystem::path(local_app_data) / "AudioMasteringTool" / "settings.json";
  }
#endif
  return std::filesystem::temp_directory_path() / "AudioMasteringTool" / "settings.json";
}

SettingsManager::SettingsManager(std::filesystem::path settings_path)
    : path_(settings_path.empty() ? default_settings_path() : std::move(settings_path)) {
  reset_to_defaults();
}

void SettingsManager::reset_to_defaults() {
  std::scoped_lock lock(mutex_);
  settings_ = AppSettings{};
#ifdef _WIN32
  const char* local_app_data = std::getenv("LOCALAPPDATA");
  if (local_app_data && *local_app_data) {
    auto base = std::filesystem::path(local_app_data) / "AudioMasteringTool";
    settings_.models_directory = base / "models";
    settings_.cache_directory = base / "cache";
  }
#endif
}

bool SettingsManager::save(std::string& error) const {
  error.clear();
  std::scoped_lock lock(mutex_);
  if (!validate_settings(settings_, error)) return false;

  std::error_code ec;
  if (!path_.parent_path().empty()) {
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) {
      error = "Failed to create directory " + path_.parent_path().string() + ": " + ec.message();
      return false;
    }
  }

  std::ofstream out(path_, std::ios::trunc);
  if (!out.is_open() || !out.good()) {
    error = "Could not open settings file for write: " + path_.string();
    return false;
  }

  std::string ep_str = "cpu";
  if (settings_.execution_provider == ExecutionProviderPreference::cuda) ep_str = "cuda";
  else if (settings_.execution_provider == ExecutionProviderPreference::directml) ep_str = "directml";

  out << "{\n"
      << "  \"schema_version\": " << settings_.schema_version << ",\n"
      << "  \"audio_output_device\": \"" << escape_json_string(settings_.audio_output_device) << "\",\n"
      << "  \"buffer_size_frames\": " << settings_.buffer_size_frames << ",\n"
      << "  \"sample_rate_hz\": " << settings_.sample_rate_hz << ",\n"
      << "  \"execution_provider\": \"" << ep_str << "\",\n"
      << "  \"models_directory\": \"" << escape_json_string(settings_.models_directory.string()) << "\",\n"
      << "  \"cache_directory\": \"" << escape_json_string(settings_.cache_directory.string()) << "\",\n"
      << "  \"max_cache_size_mb\": " << settings_.max_cache_size_mb << ",\n"
      << "  \"telemetry_enabled\": " << (settings_.telemetry_enabled ? "true" : "false") << ",\n"
      << "  \"crash_reports_enabled\": " << (settings_.crash_reports_enabled ? "true" : "false") << ",\n"
      << "  \"default_export_recipe\": \"" << escape_json_string(settings_.default_export_recipe) << "\",\n"
      << "  \"high_dpi_scaling\": " << (settings_.high_dpi_scaling ? "true" : "false") << ",\n"
      << "  \"dark_theme\": " << (settings_.dark_theme ? "true" : "false") << ",\n"
      << "  \"active_preference_profile\": \"" << escape_json_string(settings_.active_preference_profile) << "\"\n"
      << "}\n";

  out.flush();
  if (!out.good()) {
    error = "Failed to write complete settings data to " + path_.string();
    return false;
  }

  return true;
}

bool SettingsManager::load(std::string& error) {
  error.clear();
  std::scoped_lock lock(mutex_);

  std::error_code ec;
  if (!std::filesystem::exists(path_, ec)) {
    // If no file exists yet, return default settings cleanly
    return true;
  }

  std::ifstream in(path_);
  if (!in.is_open()) {
    error = "Could not open settings file: " + path_.string();
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (content.empty()) {
    return true;
  }

  SimpleJsonParser parser(content);
  JsonValue root;
  if (!parser.parse(root, error)) {
    error = "Failed to parse settings JSON: " + error;
    return false;
  }

  if (root.type != JsonValue::Type::Object) {
    error = "Settings JSON root must be an object";
    return false;
  }

  const auto& obj = root.obj_val;

  auto get_str_val = [&](const std::string& key, const std::string& def) -> std::string {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.type == JsonValue::Type::String) {
      return it->second.str_val;
    }
    return def;
  };

  auto get_int_val = [&](const std::string& key, int def) -> int {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.type == JsonValue::Type::Number &&
        std::isfinite(it->second.num_val) &&
        it->second.num_val >= static_cast<double>(std::numeric_limits<int>::min()) &&
        it->second.num_val <= static_cast<double>(std::numeric_limits<int>::max())) {
      return static_cast<int>(it->second.num_val);
    }
    return def;
  };

  auto get_bool_val = [&](const std::string& key, bool def) -> bool {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.type == JsonValue::Type::Bool) {
      return it->second.bool_val;
    }
    return def;
  };

  AppSettings loaded = settings_;
  loaded.schema_version = get_int_val("schema_version", 1);
  loaded.audio_output_device = get_str_val("audio_output_device", "");
  loaded.buffer_size_frames = get_int_val("buffer_size_frames", 512);
  loaded.sample_rate_hz = get_int_val("sample_rate_hz", 44100);

  auto ep_it = obj.find("execution_provider");
  if (ep_it != obj.end()) {
    if (ep_it->second.type == JsonValue::Type::String) {
      if (ep_it->second.str_val == "cuda") loaded.execution_provider = ExecutionProviderPreference::cuda;
      else if (ep_it->second.str_val == "directml") loaded.execution_provider = ExecutionProviderPreference::directml;
      else loaded.execution_provider = ExecutionProviderPreference::cpu;
    } else if (ep_it->second.type == JsonValue::Type::Number) {
      int ep_int = static_cast<int>(ep_it->second.num_val);
      if (ep_int == 1) loaded.execution_provider = ExecutionProviderPreference::cuda;
      else if (ep_int == 2) loaded.execution_provider = ExecutionProviderPreference::directml;
      else loaded.execution_provider = ExecutionProviderPreference::cpu;
    }
  }

  std::string mod_dir = get_str_val("models_directory", "");
  if (!mod_dir.empty()) loaded.models_directory = mod_dir;

  std::string cache_dir = get_str_val("cache_directory", "");
  if (!cache_dir.empty()) loaded.cache_directory = cache_dir;

  const int cache_budget = get_int_val("max_cache_size_mb", 2048);
  if (cache_budget < 0) {
    error = "Cache budget cannot be negative";
    return false;
  }
  loaded.max_cache_size_mb = static_cast<std::size_t>(cache_budget);
  loaded.telemetry_enabled = get_bool_val("telemetry_enabled", false);
  loaded.crash_reports_enabled = get_bool_val("crash_reports_enabled", false);
  loaded.default_export_recipe = get_str_val("default_export_recipe", "studio_master");
  loaded.high_dpi_scaling = get_bool_val("high_dpi_scaling", true);
  loaded.dark_theme = get_bool_val("dark_theme", true);
  loaded.active_preference_profile = get_str_val("active_preference_profile", "default");

  if (!validate_settings(loaded, error)) return false;
  settings_ = std::move(loaded);

  return true;
}

}  // namespace amt::settings
