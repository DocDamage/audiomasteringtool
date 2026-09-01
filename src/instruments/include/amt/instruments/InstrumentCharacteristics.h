#pragma once

#include <cstddef>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::instruments {

struct InstrumentCharacteristics {
  double f0_hz{0.0};
  double spectral_centroid_hz{0.0};
  double spectral_spread_hz{0.0};
  double harmonicity_ratio{0.0};
  double transient_density{0.0};
  double attack_time_ms{0.0};
  double decay_sustain_ratio{0.0};
  double crest_factor_db{0.0};
  double stereo_correlation{1.0};
  double dynamic_range_lu{0.0};
};

[[nodiscard]] InstrumentCharacteristics extract_characteristics(
    const amt::audio::AudioBuffer& buffer,
    int sample_rate = 44100,
    std::size_t start_frame = 0,
    std::size_t frame_count = 0);

[[nodiscard]] double estimate_f0(
    const float* samples,
    std::size_t count,
    int sample_rate);

[[nodiscard]] double compute_spectral_centroid(
    const float* samples,
    std::size_t count,
    int sample_rate);

[[nodiscard]] double compute_stereo_correlation(
    const amt::audio::AudioBuffer& buffer,
    std::size_t start_frame,
    std::size_t count);

}  // namespace amt::instruments
