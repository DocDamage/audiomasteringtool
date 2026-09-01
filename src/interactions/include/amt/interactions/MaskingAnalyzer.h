#pragma once

#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/interactions/BassTracker.h"
#include "amt/interactions/InteractionEngine.h"
#include "amt/interactions/KickTracker.h"

namespace amt::interactions {

class MaskingAnalyzer {
 public:
  MaskingAnalyzer() = default;
  ~MaskingAnalyzer() = default;

  [[nodiscard]] InteractionEvidence evaluate_kick_bass_interaction(
      const KickAnalysis& kick,
      const BassAnalysis& bass,
      const amt::audio::AudioBuffer& full_mix) const;
};

}  // namespace amt::interactions
