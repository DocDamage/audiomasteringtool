#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::analysis {

struct ResonanceCandidate {
  double frequency_hz{0.0};
  double prominence_db{0.0};
  double persistence{0.0};
  double severity{0.0};
  double first_seen_seconds{0.0};
  double last_seen_seconds{0.0};
};

struct PerceptualMetrics {
  double harshness_score{0.0};
  double mud_score{0.0};
  double sub_buildup_score{0.0};
  double brightness_score{0.0};
  double tonal_imbalance_score{0.0};
  std::vector<ResonanceCandidate> resonances;
};

class PerceptualAnalyzer {
 public:
  PerceptualAnalyzer(int sample_rate, std::size_t channels);
  ~PerceptualAnalyzer();
  PerceptualAnalyzer(const PerceptualAnalyzer&) = delete;
  PerceptualAnalyzer& operator=(const PerceptualAnalyzer&) = delete;
  PerceptualAnalyzer(PerceptualAnalyzer&&) noexcept;
  PerceptualAnalyzer& operator=(PerceptualAnalyzer&&) noexcept;

  void process(const amt::audio::AudioBuffer& buffer);
  [[nodiscard]] PerceptualMetrics finalize();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace amt::analysis
