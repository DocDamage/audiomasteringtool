#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::analysis {

struct IntegrityMetrics {
  std::uint64_t nan_samples{0};
  std::uint64_t infinite_samples{0};
  std::uint64_t clipped_samples{0};
  std::uint64_t repeated_full_scale_runs{0};
  std::uint32_t longest_full_scale_run{0};
  double max_absolute_dc_offset{0.0};
  double channel_imbalance_db{0.0};
  double head_silence_seconds{0.0};
  double tail_silence_seconds{0.0};
};

class IntegrityAnalyzer {
 public:
  IntegrityAnalyzer(int sample_rate, std::size_t channels);
  ~IntegrityAnalyzer();
  IntegrityAnalyzer(const IntegrityAnalyzer&) = delete;
  IntegrityAnalyzer& operator=(const IntegrityAnalyzer&) = delete;
  IntegrityAnalyzer(IntegrityAnalyzer&&) noexcept;
  IntegrityAnalyzer& operator=(IntegrityAnalyzer&&) noexcept;

  void process(const amt::audio::AudioBuffer& buffer);
  [[nodiscard]] IntegrityMetrics finalize() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace amt::analysis
