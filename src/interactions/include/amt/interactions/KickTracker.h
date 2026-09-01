#pragma once

#include <cstddef>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::interactions {

struct KickEvent {
  double time_seconds{0.0};
  double peak_amplitude{0.0};
  double fundamental_hz{55.0};
  double decay_time_ms{0.0};
  double click_transient_ratio{0.0};
};

struct KickAnalysis {
  std::vector<KickEvent> kicks;
  double average_fundamental_hz{55.0};
  double average_decay_ms{120.0};
  double estimated_tempo_bpm{120.0};
  double regularity{0.0};
};

class KickTracker {
 public:
  KickTracker() = default;
  ~KickTracker() = default;

  [[nodiscard]] KickAnalysis track_kicks(
      const amt::audio::AudioBuffer& audio,
      const float* stem_audio = nullptr,
      std::size_t stem_frames = 0,
      int sample_rate = 44100) const;
};

}  // namespace amt::interactions
