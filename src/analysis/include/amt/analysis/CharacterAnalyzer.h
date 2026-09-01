#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "amt/analysis/IntegrityAnalyzer.h"
#include "amt/audio/AudioBuffer.h"

namespace amt::analysis {

struct CharacterMetrics {
  std::uint64_t near_full_scale_samples{0U};
  std::uint64_t plateau_samples{0U};
  std::size_t active_windows{0U};
  std::size_t clipping_windows{0U};
  double high_level_sample_fraction{0.0};
  double clipping_window_fraction{0.0};
  double median_window_crest_db{0.0};
  double hard_clip_likelihood{0.0};
  double saturation_likelihood{0.0};
  double intentional_character_likelihood{0.0};
  double accidental_defect_risk{0.0};
  double inference_confidence{0.0};
};

class CharacterAnalyzer {
 public:
  CharacterAnalyzer(int sample_rate, std::size_t channels);
  ~CharacterAnalyzer();
  CharacterAnalyzer(const CharacterAnalyzer&) = delete;
  CharacterAnalyzer& operator=(const CharacterAnalyzer&) = delete;
  CharacterAnalyzer(CharacterAnalyzer&&) noexcept;
  CharacterAnalyzer& operator=(CharacterAnalyzer&&) noexcept;

  void process(const amt::audio::AudioBuffer& buffer);
  [[nodiscard]] CharacterMetrics finalize(const IntegrityMetrics& integrity);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace amt::analysis
