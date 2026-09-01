#pragma once

#include "amt/audio/AudioBuffer.h"

namespace amt::repair {

struct TransientRepairSettings {
  double attack_boost_db{2.0};
  double sensitivity{0.5};
  double max_boost_db{4.0};
};

struct TransientRepairReport {
  double average_attack_boost_applied_db{0.0};
  std::size_t transients_enhanced{0};
  bool applied{false};
};

[[nodiscard]] TransientRepairReport process_transient_repair(
    amt::audio::AudioBuffer& buffer,
    const TransientRepairSettings& settings = {},
    int sample_rate = 44100);

}  // namespace amt::repair
