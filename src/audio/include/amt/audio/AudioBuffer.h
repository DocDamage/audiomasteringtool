#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace amt::audio {

class AudioBuffer {
 public:
  AudioBuffer() = default;
  AudioBuffer(std::size_t channels, std::size_t frames);

  void resize(std::size_t channels, std::size_t frames);
  void resize_frames(std::size_t frames);
  void clear() noexcept;

  [[nodiscard]] std::size_t channels() const noexcept { return channels_.size(); }
  [[nodiscard]] std::size_t frames() const noexcept {
    return channels_.empty() ? 0U : channels_.front().size();
  }
  [[nodiscard]] bool empty() const noexcept { return frames() == 0U; }

  [[nodiscard]] std::span<float> channel(std::size_t index);
  [[nodiscard]] std::span<const float> channel(std::size_t index) const;

  void to_interleaved(std::vector<float>& output) const;
  static AudioBuffer from_interleaved(std::span<const float> input, std::size_t channels);

 private:
  std::vector<std::vector<float>> channels_;
};

}  // namespace amt::audio
