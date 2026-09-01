#pragma once

#include <cstddef>
#include <memory>

#include "amt/audio/AudioBuffer.h"

namespace amt::analysis {

struct StereoMetrics {
  double correlation{1.0};
  double low_band_width{0.0};
  double mid_band_width{0.0};
  double high_band_width{0.0};
  double mono_fold_down_delta_db{0.0};
  double negative_correlation_window_fraction{0.0};
};

class StereoAnalyzer {
 public:
  StereoAnalyzer(int sample_rate, std::size_t channels);
  ~StereoAnalyzer();
  StereoAnalyzer(const StereoAnalyzer&) = delete;
  StereoAnalyzer& operator=(const StereoAnalyzer&) = delete;
  StereoAnalyzer(StereoAnalyzer&&) noexcept;
  StereoAnalyzer& operator=(StereoAnalyzer&&) noexcept;

  void process(const amt::audio::AudioBuffer& buffer);
  [[nodiscard]] StereoMetrics finalize() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace amt::analysis
