#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "amt/audio/AudioBuffer.h"

namespace amt::playback {

struct AudioOutputConfig {
  int sample_rate{0};
  std::size_t channels{0};
  std::size_t frames_per_buffer{1024U};
  std::size_t queued_buffers{4U};
};

using AudioRenderCallback =
    std::function<std::size_t(amt::audio::AudioBuffer&, std::size_t)>;

class IAudioOutputDevice {
 public:
  virtual ~IAudioOutputDevice() = default;
  virtual bool open(AudioOutputConfig config, AudioRenderCallback callback,
                    std::string& error) = 0;
  virtual bool start(std::string& error) = 0;
  virtual bool pause(std::string& error) = 0;
  virtual bool resume(std::string& error) = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual bool running() const noexcept = 0;
  [[nodiscard]] virtual std::string backend_name() const = 0;
};

[[nodiscard]] std::unique_ptr<IAudioOutputDevice> make_default_audio_output_device();

}  // namespace amt::playback
