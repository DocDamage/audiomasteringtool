#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/core/JobControl.h"

namespace amt::codec {

enum class AudioContainer { unknown, wav, aiff, flac, ogg, mp3, aac_m4a, opus };
enum class AudioSampleFormat { unknown, pcm16, pcm24, pcm32, float32, float64, compressed };

struct AudioMetadata {
  std::int64_t frames{0};
  int sample_rate{0};
  int channels{0};
  int bit_depth{0};
  bool seekable{false};
  AudioContainer container{AudioContainer::unknown};
  AudioSampleFormat sample_format{AudioSampleFormat::unknown};
  std::string container_name;
  std::string sample_format_name;
  std::map<std::string, std::string> tags;
};

struct CodecCapability {
  AudioContainer container{AudioContainer::unknown};
  std::string name;
  std::vector<std::string> extensions;
  bool decode{false};
  bool encode{false};
  bool lossless{false};
};

struct EncodeSettings {
  int sample_rate{0};
  int channels{0};
  AudioContainer container{AudioContainer::unknown};
  AudioSampleFormat sample_format{AudioSampleFormat::unknown};
  std::map<std::string, std::string> tags;
};

class IAudioDecoder {
 public:
  virtual ~IAudioDecoder() = default;
  [[nodiscard]] virtual const AudioMetadata& metadata() const noexcept = 0;
  [[nodiscard]] virtual std::int64_t tell() const noexcept = 0;
  virtual bool seek(std::int64_t frame, std::string& error) = 0;
  virtual bool read(amt::audio::AudioBuffer& output, std::size_t max_frames,
                    std::size_t& frames_read, std::string& error,
                    const amt::core::CancellationToken* cancellation = nullptr) = 0;
};

class IAudioEncoder {
 public:
  virtual ~IAudioEncoder() = default;
  virtual bool write(const amt::audio::AudioBuffer& input, std::string& error,
                     const amt::core::CancellationToken* cancellation = nullptr) = 0;
  virtual bool finalize(std::string& error) = 0;
};

class ICodecService {
 public:
  virtual ~ICodecService() = default;
  [[nodiscard]] virtual bool available() const noexcept = 0;
  [[nodiscard]] virtual std::string backend_name() const = 0;
  [[nodiscard]] virtual std::string backend_error() const = 0;
  [[nodiscard]] virtual std::vector<CodecCapability> capabilities() const = 0;
  [[nodiscard]] virtual std::optional<AudioMetadata> probe(
      const std::filesystem::path& path, std::string& error) const = 0;
  [[nodiscard]] virtual std::unique_ptr<IAudioDecoder> open_decoder(
      const std::filesystem::path& path, std::string& error) const = 0;
  [[nodiscard]] virtual std::unique_ptr<IAudioEncoder> open_encoder(
      const std::filesystem::path& path, const EncodeSettings& settings,
      std::string& error) const = 0;
};

struct ExportRequest {
  std::optional<int> sample_rate;
  std::optional<AudioContainer> container;
  std::optional<AudioSampleFormat> sample_format;
  bool dither_when_reducing_integer_depth{true};
};

bool export_audio(ICodecService& codecs, const std::filesystem::path& input,
                  const std::filesystem::path& output, const ExportRequest& request,
                  std::string& error,
                  const amt::core::CancellationToken* cancellation = nullptr,
                  const amt::core::ProgressCallback& progress = {});

[[nodiscard]] AudioContainer container_from_extension(const std::filesystem::path& path);
[[nodiscard]] int integer_bit_depth(AudioSampleFormat format) noexcept;

}  // namespace amt::codec
