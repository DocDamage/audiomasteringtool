#pragma once

#include <string>
#include <vector>
#include "amt/audio/AudioBuffer.h"

namespace amt::translation {

struct ElementSurvivalMetrics {
  std::string element_name;
  double small_speaker_audibility{1.0}; // [0.0, 1.0]
  double mono_survival_ratio{1.0};      // [0.0, 1.0]
  bool has_audibility_risk{false};
  std::string recommendation;
};

class InstrumentSurvivalEvaluator {
 public:
  [[nodiscard]] static std::vector<ElementSurvivalMetrics> evaluate_survival(
      const amt::audio::AudioBuffer& full_mix,
      const amt::audio::AudioBuffer& small_speaker_mix,
      const amt::audio::AudioBuffer& mono_mix);
};

}  // namespace amt::translation
