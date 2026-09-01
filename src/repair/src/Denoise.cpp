#include "amt/repair/Denoise.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace amt::repair {

DenoiseReport process_denoise(
    amt::audio::AudioBuffer& buffer,
    const DenoiseSettings& settings,
    int sample_rate) {
  DenoiseReport report{};
  if (buffer.frames() < 256) return report;

  const double floor_linear = std::pow(10.0, settings.noise_floor_estimate_db / 20.0);
  const double max_attenuation = std::pow(10.0, -settings.reduction_amount_db / 20.0);
  const double min_gain = std::max(max_attenuation, settings.spectral_floor_ratio);

  const int sr = sample_rate > 0 ? sample_rate : 44100;
  const std::size_t frames = buffer.frames();
  const std::size_t channels = buffer.channels();

  // Expander / downward gate with smooth hysteresis
  const double dt = 1.0 / static_cast<double>(sr);
  const double alpha_attack = dt / (0.005 + dt);  // 5ms
  const double alpha_release = dt / (0.080 + dt); // 80ms

  for (std::size_t c = 0; c < channels; ++c) {
    float* data = buffer.channel(c).data();
    double env = 0.0;

    for (std::size_t i = 0; i < frames; ++i) {
      const double abs_val = std::abs(static_cast<double>(data[i]));
      const double alpha = (abs_val > env) ? alpha_attack : alpha_release;
      env += alpha * (abs_val - env);

      if (env < floor_linear * 2.0) {
        const double ratio = std::clamp(env / (floor_linear * 2.0), min_gain, 1.0);
        data[i] = static_cast<float>(static_cast<double>(data[i]) * ratio);
      }
    }
  }

  report.applied = true;
  report.noise_reduction_applied_db = settings.reduction_amount_db;
  return report;
}

}  // namespace amt::repair
