#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::analysis {

struct SpectrumBand {
  double low_hz{0.0};
  double high_hz{0.0};
  double energy_ratio{0.0};
};

struct SpectrumMetrics {
  double centroid_hz{0.0};
  double rolloff_85_hz{0.0};
  std::vector<SpectrumBand> bands;
};

class SpectrumAnalyzer {
 public:
  SpectrumAnalyzer(int sample_rate, std::size_t channels, std::size_t fft_size = 2048U);
  ~SpectrumAnalyzer();
  SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
  SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;
  SpectrumAnalyzer(SpectrumAnalyzer&&) noexcept;
  SpectrumAnalyzer& operator=(SpectrumAnalyzer&&) noexcept;

  void process(const amt::audio::AudioBuffer& buffer);
  [[nodiscard]] SpectrumMetrics finalize();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace amt::analysis
