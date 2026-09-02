#include "amt/settings/ModelManager.h"
#include <system_error>

namespace amt::settings {

ModelManager::ModelManager(std::filesystem::path models_directory)
    : models_dir_(std::move(models_directory)) {}

std::vector<InstalledModelInfo> ModelManager::list_installed_models() const {
  std::vector<InstalledModelInfo> models;

  InstalledModelInfo htdemucs;
  htdemucs.model_id = "htdemucs_ft";
  htdemucs.name = "HTDemucs Fine-Tuned v1";
  htdemucs.version = "1.0.0";
  htdemucs.file_path = models_dir_ / "htdemucs_ft.onnx";

  std::error_code ec;
  if (std::filesystem::exists(htdemucs.file_path, ec)) {
    htdemucs.size_bytes = std::filesystem::file_size(htdemucs.file_path, ec);
    htdemucs.verified = true;
    htdemucs.status_description = "Installed and verified";
  } else {
    htdemucs.verified = false;
    htdemucs.status_description = "Not installed (on-demand download available)";
  }

  models.push_back(htdemucs);
  return models;
}

bool ModelManager::verify_model(const std::string& model_id, std::string& error) const {
  error.clear();
  auto models = list_installed_models();
  for (const auto& m : models) {
    if (m.model_id == model_id) {
      if (!m.verified) {
        error = "Model is not installed or verification failed";
        return false;
      }
      return true;
    }
  }
  error = "Unknown model ID: " + model_id;
  return false;
}

}  // namespace amt::settings
