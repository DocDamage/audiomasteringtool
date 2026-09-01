#pragma once

#include <map>
#include <string>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/instruments/InstrumentCharacteristics.h"
#include "amt/instruments/InstrumentEvent.h"
#include "amt/instruments/InstrumentTaxonomy.h"

namespace amt::instruments {

struct SeparationStems {
  amt::audio::AudioBuffer drums;
  amt::audio::AudioBuffer bass;
  amt::audio::AudioBuffer vocals;
  amt::audio::AudioBuffer other;
  bool available{false};
};

class SeparationAssistedDetector {
 public:
  SeparationAssistedDetector() = default;
  ~SeparationAssistedDetector() = default;

  [[nodiscard]] std::vector<InstrumentEvent> detect_instruments(
      const amt::audio::AudioBuffer& full_mix,
      const SeparationStems* stems = nullptr,
      int sample_rate = 44100) const;
};

}  // namespace amt::instruments
