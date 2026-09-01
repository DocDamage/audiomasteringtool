#pragma once

#include "amt/audio/AudioBuffer.h"
#include "amt/reference/ReferenceProfile.h"

namespace amt::reference {

class ReferenceAnalyzer {
 public:
  ReferenceAnalyzer() = default;
  ~ReferenceAnalyzer() = default;

  [[nodiscard]] ReferenceProfile extract_profile(
      const amt::audio::AudioBuffer& audio,
      const std::string& name = "Reference Track",
      int sample_rate = 44100) const;
};

}  // namespace amt::reference
