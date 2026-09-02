#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace amt::settings {

struct InstalledModelInfo {
  std::string model_id{"htdemucs_ft"};
  std::string name{"HTDemucs Fine-Tuned v1"};
  std::string version{"1.0.0"};
  std::filesystem::path file_path;
  std::uintmax_t size_bytes{0};
  bool verified{false};
  std::string expected_sha256;
  std::string status_description;
};

class ModelManager {
 public:
  explicit ModelManager(std::filesystem::path models_directory);

  [[nodiscard]] std::vector<InstalledModelInfo> list_installed_models() const;
  [[nodiscard]] bool verify_model(const std::string& model_id, std::string& error) const;

 private:
  std::filesystem::path models_dir_;
};

}  // namespace amt::settings
