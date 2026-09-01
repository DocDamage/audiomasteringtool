#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::audio {

struct WaveformPeakBin {
  float minimum{0.0F};
  float maximum{0.0F};
  float rms{0.0F};
  std::uint32_t frames{0};
};

struct WaveformLevel {
  std::uint64_t frames_per_bin{0};
  std::vector<std::vector<WaveformPeakBin>> channels;
};

struct WaveformPeakCache {
  int sample_rate{0};
  std::int64_t source_frames{0};
  std::vector<WaveformLevel> levels;
};

class WaveformPeakAccumulator {
 public:
  WaveformPeakAccumulator(int sample_rate, std::size_t channels,
                          std::uint64_t base_frames_per_bin = 256U,
                          std::uint32_t level_factor = 4U);

  void append(const AudioBuffer& buffer);
  [[nodiscard]] WaveformPeakCache finalize();

 private:
  void flush_base_bin();

  int sample_rate_{0};
  std::size_t channels_{0};
  std::uint64_t base_frames_per_bin_{256};
  std::uint32_t level_factor_{4};
  std::int64_t source_frames_{0};
  std::uint64_t pending_frames_{0};
  std::vector<float> pending_min_;
  std::vector<float> pending_max_;
  std::vector<double> pending_sum_squares_;
  std::vector<std::vector<WaveformPeakBin>> base_bins_;
};

}  // namespace amt::audio
