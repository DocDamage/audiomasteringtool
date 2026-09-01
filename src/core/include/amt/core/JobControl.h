#pragma once

#include <atomic>
#include <functional>

namespace amt::core {

class CancellationToken {
 public:
  void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
  [[nodiscard]] bool is_cancelled() const noexcept {
    return cancelled_.load(std::memory_order_acquire);
  }

 private:
  std::atomic_bool cancelled_{false};
};

using ProgressCallback = std::function<void(double)>;

inline void report_progress(const ProgressCallback& callback, const double value) {
  if (callback) callback(value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value));
}

}  // namespace amt::core
