#pragma once

#include "amt/audio/AudioBuffer.h"
#include "amt/translation/PlaybackClass.h"

namespace amt::translation {

class TranslationModel {
 public:
  [[nodiscard]] static amt::audio::AudioBuffer simulate(
      const amt::audio::AudioBuffer& input,
      const PlaybackClassInfo& target_class);
};

}  // namespace amt::translation
