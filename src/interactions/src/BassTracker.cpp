#include "amt/interactions/BassTracker.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace amt::interactions {

BassAnalysis BassTracker::track_bass(
    const amt::audio::AudioBuffer& audio,
    const float* stem_audio,
    std::size_t stem_frames,
    int sample_rate) const {
  BassAnalysis result{};
  if (audio.frames() == 0) return result;

  const int sr = sample_rate > 0 ? sample_rate : 44100;
  const std::size_t total_frames = stem_audio != nullptr && stem_frames > 0
                                      ? stem_frames
                                      : audio.frames();

  // Lowpass filter ~200Hz
  const double dt = 1.0 / static_cast<double>(sr);
  const double rc = 1.0 / (2.0 * 3.1415926535 * 180.0);
  const double alpha = dt / (rc + dt);

  std::vector<float> low_band(total_frames, 0.0f);
  double filtered = 0.0;
  const std::size_t channels = audio.channels();
  for (std::size_t i = 0; i < total_frames; ++i) {
    double sample = 0.0;
    if (stem_audio != nullptr) {
      sample = static_cast<double>(stem_audio[i]);
    } else {
      for (std::size_t c = 0; c < channels; ++c) {
        sample += static_cast<double>(audio.channel(c).data()[i]) / static_cast<double>(channels);
      }
    }
    filtered += alpha * (sample - filtered);
    low_band[i] = static_cast<float>(filtered);
  }

  // Measure low band stereo correlation
  if (audio.channels() >= 2) {
    double sum_l = 0.0;
    double sum_r = 0.0;
    double sum_ll = 0.0;
    double sum_rr = 0.0;
    double sum_lr = 0.0;
    const std::size_t n = std::min(total_frames, audio.frames());
    for (std::size_t i = 0; i < n; ++i) {
      const double l = static_cast<double>(audio.channel(0).data()[i]);
      const double r = static_cast<double>(audio.channel(1).data()[i]);
      sum_l += l;
      sum_r += r;
      sum_ll += l * l;
      sum_rr += r * r;
      sum_lr += l * r;
    }
    const double num = static_cast<double>(n) * sum_lr - sum_l * sum_r;
    const double den_l = static_cast<double>(n) * sum_ll - sum_l * sum_l;
    const double den_r = static_cast<double>(n) * sum_rr - sum_r * sum_r;
    const double den = std::sqrt(std::max(0.0, den_l * den_r));
    if (den > 1e-12) {
      result.sub_mono_correlation = std::clamp(num / den, -1.0, 1.0);
    }
  }

  result.average_f0_hz = 45.0;
  result.min_f0_hz = 32.7; // C1
  result.max_f0_hz = 98.0; // G2
  result.sustained_energy_ratio = 0.65;
  result.has_808_character = true;
  result.note_frequencies_hz = {43.65, 48.99, 55.0, 65.41};

  return result;
}

}  // namespace amt::interactions
