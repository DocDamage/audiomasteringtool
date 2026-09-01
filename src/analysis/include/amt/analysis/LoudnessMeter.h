#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::analysis {

struct LoudnessPoint {
  double time_seconds{0.0};
  double momentary_lufs{0.0};
  double short_term_lufs{0.0};
};

struct LoudnessMetrics {
  double integrated_lufs{0.0};
  double loudness_range_lu{0.0};
  double max_momentary_lufs{0.0};
  double max_short_term_lufs{0.0};
  double short_term_variation_lu{0.0};
  double sample_peak_dbfs{0.0};
  double true_peak_dbtp{0.0};
  double crest_factor_db{0.0};
  double peak_to_loudness_ratio_db{0.0};
  std::vector<LoudnessPoint> timeline;
};

class LoudnessMeter {
 public:
  LoudnessMeter(int sample_rate, std::size_t channels);
  ~LoudnessMeter();
  LoudnessMeter(const LoudnessMeter&) = delete;
  LoudnessMeter& operator=(const LoudnessMeter&) = delete;
  LoudnessMeter(LoudnessMeter&&) noexcept;
  LoudnessMeter& operator=(LoudnessMeter&&) noexcept;

  void process(const amt::audio::AudioBuffer& buffer);
  [[nodiscard]] LoudnessMetrics finalize();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace amt::analysis
