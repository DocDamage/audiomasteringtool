#pragma once

#include "amt/audio/AudioBuffer.h"
#include "amt/translation/TranslationScore.h"

namespace amt::translation {

class TranslationAnalyzer {
 public:
  [[nodiscard]] static TranslationReport analyze(
      const amt::audio::AudioBuffer& buffer);
};

}  // namespace amt::translation
