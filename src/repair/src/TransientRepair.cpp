#include "amt/repair/TransientRepair.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace amt::repair {

TransientRepairReport process_transient_repair(
    amt::audio::AudioBuffer& buffer,
    const TransientRepairSettings& settings,
    int sample_rate) {
  TransientRepairReport report{};
  if (buffer.frames() < 128) return report;

  const int sr = sample_rate > 0 ? sample_rate : 44100;
  const std::size_t frames = buffer.frames();
  const std::size_t channels = buffer.channels();

  // Fast envelope vs slow envelope differential detector
  const double dt = 1.0 / static_cast<double>(sr);
  const double alpha_fast = dt / (0.002 + dt); // 2ms attack follower
  const double alpha_slow = dt / (0.050 + dt); // 50ms average follower

  const double max_mult = std::pow(10.0, std::min(settings.attack_boost_db, settings.max_boost_db) / 20.0);

  for (std::size_t c = 0; c < channels; ++c) {
    float* data = buffer.channel(c).data();
    double fast_env = 0.0;
    double slow_env = 0.0;

    for (std::size_t i = 0; i < frames; ++i) {
      const double abs_val = std::abs(static_cast<double>(data[i]));

      fast_env += alpha_fast * (abs_val - fast_env);
      slow_env += alpha_slow * (abs_val - slow_env);

      // When fast envelope exceeds slow envelope by threshold ratio, transient is occurring
      if (fast_env > slow_env * (1.0 + settings.sensitivity) && slow_env > 1e-4) {
        const double diff_ratio = (fast_env - slow_env) / fast_env;
        const double gain = 1.0 + (max_mult - 1.0) * std::clamp(diff_ratio, 0.0, 1.0);
        data[i] = static_cast<float>(std::clamp(static_cast<double>(data[i]) * gain, -1.0, 1.0));
        if (c == 0) {
          ++report.transients_enhanced;
        }
      }
    }
  }

  if (report.transients_enhanced > 0) {
    report.applied = true;
    report.average_attack_boost_applied_db = settings.attack_boost_db;
  }

  return report;
}

}  // namespace amt::repair
