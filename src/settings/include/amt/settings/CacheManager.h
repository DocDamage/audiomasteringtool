#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace amt::settings {

struct CacheItem {
  std::filesystem::path path;
  std::uintmax_t size_bytes{0};
  std::int64_t last_modified_ms{0};
  bool is_project_owned{false};
};

struct CacheInventory {
  std::size_t total_items{0};
  std::uintmax_t total_bytes{0};
  double total_mb{0.0};
  std::vector<CacheItem> items;
};

class CacheManager {
 public:
  explicit CacheManager(std::filesystem::path cache_directory);

  [[nodiscard]] CacheInventory inspect_cache() const;
  bool evict_to_budget(std::size_t max_budget_mb, std::string& error);
  bool clear_all_disposable(std::string& error);

 private:
  std::filesystem::path cache_dir_;
};

}  // namespace amt::settings
