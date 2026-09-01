#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "amt/codec/AudioIO.h"
#include "amt/playback/AudioDevice.h"
#include "amt/playback/Transport.h"

namespace amt::playback {

enum class ComparisonSource : int { original = 0, master_a = 1, master_b = 2 };

struct ComparisonItem {
  std::filesystem::path path;
  double audition_gain_db{0.0};
};

class ComparisonTransport {
 public:
  explicit ComparisonTransport(
      amt::codec::ICodecService& codecs,
      std::unique_ptr<IAudioOutputDevice> device = make_default_audio_output_device());
  ~ComparisonTransport();
  ComparisonTransport(const ComparisonTransport&) = delete;
  ComparisonTransport& operator=(const ComparisonTransport&) = delete;

  bool load(const ComparisonItem& original, const ComparisonItem& master_a,
            const ComparisonItem& master_b, std::string& error);
  void select(ComparisonSource source) noexcept;
  bool play(std::string& error);
  bool pause(std::string& error);
  bool resume(std::string& error);
  bool seek(std::int64_t frame, std::string& error);
  void stop() noexcept;
  void wait_until_finished() const;

  [[nodiscard]] TransportState state() const noexcept {
    return state_.load(std::memory_order_acquire);
  }
  [[nodiscard]] ComparisonSource selected_source() const noexcept {
    return static_cast<ComparisonSource>(selected_.load(std::memory_order_acquire));
  }
  [[nodiscard]] std::int64_t playhead_frame() const noexcept {
    return playhead_frame_.load(std::memory_order_acquire);
  }
  [[nodiscard]] const amt::codec::AudioMetadata* metadata() const noexcept;
  [[nodiscard]] std::string output_backend_name() const;

 private:
  bool start_device(std::string& error);
  std::size_t render(amt::audio::AudioBuffer& output, std::size_t requested_frames);

  amt::codec::ICodecService& codecs_;
  std::unique_ptr<IAudioOutputDevice> device_;
  std::array<std::unique_ptr<amt::codec::IAudioDecoder>, 3> decoders_;
  std::array<double, 3> gains_{{1.0, 1.0, 1.0}};
  mutable std::mutex decoder_mutex_;
  std::atomic<TransportState> state_{TransportState::empty};
  std::atomic<int> selected_{0};
  std::atomic<std::int64_t> playhead_frame_{0};
  int render_source_{0};
  int previous_source_{0};
  std::size_t crossfade_remaining_{0U};
  std::size_t crossfade_total_{1U};
};

}  // namespace amt::playback
