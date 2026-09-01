#include "amt/repair/Declipping.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace amt::repair {

DeclippingReport process_declipping(
    amt::audio::AudioBuffer& buffer,
    const DeclippingSettings& settings) {
  DeclippingReport report{};
  if (buffer.frames() < 4) return report;

  const float thresh = static_cast<float>(settings.threshold_linear);
  const std::size_t frames = buffer.frames();
  const std::size_t channels = buffer.channels();

  double max_restored_peak = 0.0;

  for (std::size_t c = 0; c < channels; ++c) {
    float* data = buffer.channel(c).data();
    std::size_t i = 0;
    while (i < frames) {
      if (std::abs(data[i]) >= thresh) {
        const std::size_t start = (i > 0) ? (i - 1) : 0;
        std::size_t end = i;
        while (end < frames && std::abs(data[end]) >= thresh) {
          ++end;
        }
        const std::size_t clip_len = end - i;
        report.clipped_samples_reconstructed += clip_len;

        // Cubic spline interpolation between start and end (if end < frames)
        if (clip_len > 0 && clip_len < 32 && start > 0 && end + 1 < frames) {
          const float y0 = data[start - 1];
          const float y1 = data[start];
          const float y2 = data[end];
          const float y3 = data[end + 1];

          // Compute Hermite tangents
          const float m0 = 0.5f * (y2 - y0);
          const float m1 = 0.5f * (y3 - y1);

          for (std::size_t k = 0; k < clip_len; ++k) {
            const float t = static_cast<float>(k + 1) / static_cast<float>(clip_len + 1);
            const float t2 = t * t;
            const float t3 = t2 * t;

            const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
            const float h10 = t3 - 2.0f * t2 + t;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;

            const float interpolated = h00 * y1 + h10 * m0 + h01 * y2 + h11 * m1;
            const float sign = (data[i + k] >= 0.0f) ? 1.0f : -1.0f;
            data[i + k] = sign * std::max(thresh, std::abs(interpolated));

            if (std::abs(data[i + k]) > max_restored_peak) {
              max_restored_peak = std::abs(data[i + k]);
            }
          }
        }
        i = end + 1;
      } else {
        ++i;
      }
    }
  }

  if (report.clipped_samples_reconstructed > 0) {
    report.applied = true;
    if (max_restored_peak > thresh) {
      report.peak_gain_restored_db = 20.0 * std::log10(max_restored_peak / thresh);
    }
  }

  return report;
}

}  // namespace amt::repair
