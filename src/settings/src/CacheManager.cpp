#include "amt/settings/CacheManager.h"
#include <algorithm>
#include <chrono>
#include <system_error>

namespace amt::settings {

CacheManager::CacheManager(std::filesystem::path cache_directory)
    : cache_dir_(std::move(cache_directory)) {
  std::error_code ec;
  std::filesystem::create_directories(cache_dir_, ec);
}

CacheInventory CacheManager::inspect_cache() const {
  CacheInventory inv;
  if (!std::filesystem::exists(cache_dir_)) return inv;

  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(cache_dir_, ec)) {
    if (entry.is_regular_file()) {
      CacheItem item;
      item.path = entry.path();
      item.size_bytes = entry.file_size(ec);

      auto ftime = entry.last_write_time(ec);
      auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
      item.last_modified_ms = std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();

      // Check if file is tagged as project-owned vs disposable temporary cache
      item.is_project_owned = (entry.path().extension() == ".amtproj");

      inv.total_items++;
      inv.total_bytes += item.size_bytes;
      inv.items.push_back(item);
    }
  }

  inv.total_mb = static_cast<double>(inv.total_bytes) / (1024.0 * 1024.0);
  return inv;
}

bool CacheManager::evict_to_budget(std::size_t max_budget_mb, std::string& error) {
  error.clear();
  auto inv = inspect_cache();
  std::uintmax_t max_bytes = static_cast<std::uintmax_t>(max_budget_mb) * 1024 * 1024;

  if (inv.total_bytes <= max_bytes) {
    return true; // Already within budget
  }

  // Sort disposable items oldest first
  std::vector<CacheItem> disposable;
  for (const auto& item : inv.items) {
    if (!item.is_project_owned) {
      disposable.push_back(item);
    }
  }

  std::sort(disposable.begin(), disposable.end(), [](const CacheItem& a, const CacheItem& b) {
    return a.last_modified_ms < b.last_modified_ms;
  });

  std::uintmax_t current_bytes = inv.total_bytes;
  std::error_code ec;

  for (const auto& item : disposable) {
    if (current_bytes <= max_bytes) break;
    std::filesystem::remove(item.path, ec);
    if (!ec) {
      current_bytes = (current_bytes > item.size_bytes) ? (current_bytes - item.size_bytes) : 0;
    }
  }

  return true;
}

bool CacheManager::clear_all_disposable(std::string& error) {
  error.clear();
  return evict_to_budget(0, error);
}

}  // namespace amt::settings
