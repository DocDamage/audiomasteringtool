#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "amt/audio/AudioBuffer.h"

namespace amt::audio {

struct ResamplerConfig {
  int input_sample_rate{0};
  int output_sample_rate{0};
  std::size_t channels{0};
  std::size_t half_taps{32};
  double passband_ratio{0.94};
};

class IStreamingResampler {
 public:
  virtual ~IStreamingResampler() = default;
  virtual void reset() = 0;
  [[nodiscard]] virtual AudioBuffer process(const AudioBuffer& input, bool end_of_input) = 0;
  [[nodiscard]] virtual std::int64_t input_frames_seen() const noexcept = 0;
  [[nodiscard]] virtual std::int64_t output_frames_produced() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IStreamingResampler> make_high_quality_resampler(
    const ResamplerConfig& config);

}  // namespace amt::audio
