#include "amt/settings/ModelManager.h"
#include <fstream>
#include <sstream>
#include <system_error>
#include "amt/core/FileFingerprint.h"

namespace amt::settings {

namespace {

struct RegistryModelEntry {
  std::string id;
  std::string name;
  std::string version;
  std::string artifact;
  std::string sha256;
};

std::vector<RegistryModelEntry> parse_registry(const std::filesystem::path& registry_path) {
  std::vector<RegistryModelEntry> entries;
  std::error_code ec;
  if (!std::filesystem::exists(registry_path, ec)) {
    return entries;
  }

  std::ifstream in(registry_path);
  if (!in.is_open()) {
    return entries;
  }

  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (content.empty()) {
    return entries;
  }

  auto extract_field = [](const std::string& block, const std::string& key) -> std::string {
    auto pos = block.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    auto colon = block.find(':', pos);
    if (colon == std::string::npos) return "";
    auto quote_start = block.find('"', colon + 1);
    if (quote_start == std::string::npos) return "";
    auto quote_end = block.find('"', quote_start + 1);
    if (quote_end == std::string::npos) return "";
    return block.substr(quote_start + 1, quote_end - quote_start - 1);
  };

  // Find "models": [ ... ]
  auto models_pos = content.find("\"models\"");
  if (models_pos != std::string::npos) {
    auto arr_start = content.find('[', models_pos);
    if (arr_start != std::string::npos) {
      std::size_t cur = arr_start + 1;
      while (cur < content.size()) {
        auto obj_start = content.find('{', cur);
        if (obj_start == std::string::npos) break;

        // Find matching closing brace
        int depth = 1;
        std::size_t obj_end = obj_start + 1;
        while (obj_end < content.size() && depth > 0) {
          if (content[obj_end] == '{') ++depth;
          else if (content[obj_end] == '}') --depth;
          ++obj_end;
        }

        std::string block = content.substr(obj_start, obj_end - obj_start);
        RegistryModelEntry entry;
        entry.id = extract_field(block, "id");
        entry.name = extract_field(block, "source");
        if (entry.name.empty()) entry.name = entry.id;
        entry.version = extract_field(block, "version");
        entry.artifact = extract_field(block, "artifact");
        entry.sha256 = extract_field(block, "sha256");

        if (!entry.id.empty()) {
          entries.push_back(std::move(entry));
        }

        cur = obj_end;
        auto next_comma = content.find(',', cur);
        auto next_arr_end = content.find(']', cur);
        if (next_arr_end != std::string::npos && (next_comma == std::string::npos || next_arr_end < next_comma)) {
          break;
        }
      }
    }
  }

  return entries;
}

}  // namespace

ModelManager::ModelManager(std::filesystem::path models_directory)
    : models_dir_(std::move(models_directory)) {}

std::vector<InstalledModelInfo> ModelManager::list_installed_models() const {
  std::vector<InstalledModelInfo> models;

  // Search for registry.json
  std::vector<std::filesystem::path> registry_candidates = {
      models_dir_ / "registry.json",
      models_dir_ / "models" / "registry.json",
      models_dir_.parent_path() / "models" / "registry.json",
      std::filesystem::current_path() / "models" / "registry.json",
      std::filesystem::path("models/registry.json")
  };

  std::vector<RegistryModelEntry> entries;
  for (const auto& cand : registry_candidates) {
    entries = parse_registry(cand);
    if (!entries.empty()) break;
  }

  // If no registry found, supply the default approved production model entry
  if (entries.empty()) {
    RegistryModelEntry default_entry;
    default_entry.id = "htdemucs-onnx-fp16weights";
    default_entry.name = "StemSplitio/htdemucs-onnx";
    default_entry.version = "d54ed9eb60e258ea82131c6ee14578628816456a";
    default_entry.artifact = "htdemucs-onnx/htdemucs_fp16weights.onnx";
    default_entry.sha256 = "d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a";
    entries.push_back(default_entry);
  }

  std::error_code ec;
  for (const auto& entry : entries) {
    InstalledModelInfo info;
    info.model_id = entry.id;
    info.name = entry.name;
    info.version = entry.version;
    info.expected_sha256 = entry.sha256;

    // Check candidate file locations
    std::filesystem::path target_file = models_dir_ / entry.artifact;
    if (!std::filesystem::exists(target_file, ec)) {
      target_file = models_dir_ / std::filesystem::path(entry.artifact).filename();
    }
    if (!std::filesystem::exists(target_file, ec)) {
      target_file = models_dir_ / (entry.id + ".onnx");
    }

    info.file_path = target_file;

    if (std::filesystem::exists(target_file, ec)) {
      info.size_bytes = std::filesystem::file_size(target_file, ec);
      std::string fp_err;
      auto fp = amt::core::fingerprint_file_sha256(target_file, fp_err);
      if (fp && !entry.sha256.empty()) {
        if (fp->sha256 == entry.sha256) {
          info.verified = true;
          info.status_description = "Installed and verified";
        } else {
          info.verified = false;
          info.status_description = "Installed but SHA-256 checksum mismatch";
        }
      } else if (fp) {
        info.verified = true;
        info.status_description = "Installed";
      } else {
        info.verified = false;
        info.status_description = "Failed to compute checksum: " + fp_err;
      }
    } else {
      info.verified = false;
      info.status_description = "Not installed (on-demand download available)";
    }

    models.push_back(std::move(info));
  }

  return models;
}

bool ModelManager::verify_model(const std::string& model_id, std::string& error) const {
  error.clear();
  auto models = list_installed_models();
  for (const auto& m : models) {
    if (m.model_id == model_id) {
      if (!m.verified) {
        error = m.status_description.empty() ? "Model is not installed or verification failed" : m.status_description;
        return false;
      }
      return true;
    }
  }
  error = "Unknown model ID: " + model_id;
  return false;
}

}  // namespace amt::settings
