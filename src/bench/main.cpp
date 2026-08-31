#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  double minutes = 60.0;
  if (argc == 3 && std::string(argv[1]) == "--minutes") minutes = std::stod(argv[2]);
  if (!(minutes > 0.0)) return 2;

  constexpr std::size_t sample_rate = 48000;
  constexpr std::size_t channels = 2;
  constexpr std::size_t block_frames = 4096;
  const std::size_t target_frames = static_cast<std::size_t>(minutes * 60.0 * sample_rate);
  std::vector<float> block(block_frames * channels, 0.0F);

  double checksum = 0.0;
  std::size_t processed = 0;
  const auto start = std::chrono::steady_clock::now();
  while (processed < target_frames) {
    const std::size_t frames = std::min(block_frames, target_frames - processed);
    for (std::size_t i = 0; i < frames * channels; ++i) {
      const float sample = static_cast<float>(((processed + i) % 997) / 997.0 - 0.5);
      block[i] = std::tanh(sample * 1.25F);
      checksum += block[i] * 0.000001;
    }
    processed += frames;
  }
  const auto stop = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(stop - start).count();
  const double audio_seconds = minutes * 60.0;

  std::cout << "minutes=" << minutes << " elapsed_seconds=" << seconds
            << " realtime_factor=" << (audio_seconds / std::max(seconds, 1e-9))
            << " working_buffer_bytes=" << block.size() * sizeof(float)
            << " checksum=" << checksum << '\n';
  return 0;
}
