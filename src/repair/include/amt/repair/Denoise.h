#pragma once

#include "amt/audio/AudioBuffer.h"

namespace amt::repair {

struct DenoiseSettings {
  double reduction_amount_db{4.0};
  double noise_floor_estimate_db{-65.0};
  double spectral_floor_ratio{0.15}; // preserves natural air without musical chirping
};

struct DenoiseReport {
  double noise_reduction_applied_db{0.0};
  bool applied{false};
};

[[nodiscard]] DenoiseReport process_denoise(
    amt::audio::AudioBuffer& buffer,
    const DenoiseSettings& settings = {},
    int sample_rate = 44100);

}  // namespace amt::repair
