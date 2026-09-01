#pragma once

#include <cstddef>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::interactions {

struct BassAnalysis {
  double average_f0_hz{50.0};
  double min_f0_hz{30.0};
  double max_f0_hz{120.0};
  double sustained_energy_ratio{0.0};
  double sub_mono_correlation{1.0};
  bool has_808_character{false};
  std::vector<double> note_frequencies_hz;
};

class BassTracker {
 public:
  BassTracker() = default;
  ~BassTracker() = default;

  [[nodiscard]] BassAnalysis track_bass(
      const amt::audio::AudioBuffer& audio,
      const float* stem_audio = nullptr,
      std::size_t stem_frames = 0,
      int sample_rate = 44100) const;
};

}  // namespace amt::interactions
