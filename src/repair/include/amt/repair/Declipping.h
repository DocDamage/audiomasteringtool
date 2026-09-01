#pragma once

#include "amt/audio/AudioBuffer.h"

namespace amt::repair {

struct DeclippingSettings {
  double threshold_linear{0.99};
  int interpolation_order{3}; // Cubic spline / polynomial
  double ceiling_db{-0.1};
};

struct DeclippingReport {
  std::size_t clipped_samples_reconstructed{0};
  double peak_gain_restored_db{0.0};
  bool applied{false};
};

[[nodiscard]] DeclippingReport process_declipping(
    amt::audio::AudioBuffer& buffer,
    const DeclippingSettings& settings = {});

}  // namespace amt::repair
