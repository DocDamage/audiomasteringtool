#pragma once

#include <memory>
#include <string>
#include <vector>

#include "amt/codec/AudioIO.h"

namespace amt::codec {

class MediaFoundationCodecService final : public ICodecService {
 public:
  struct Impl;

  MediaFoundationCodecService();
  ~MediaFoundationCodecService() override;
  MediaFoundationCodecService(const MediaFoundationCodecService&) = delete;
  MediaFoundationCodecService& operator=(const MediaFoundationCodecService&) = delete;
  MediaFoundationCodecService(MediaFoundationCodecService&&) noexcept;
  MediaFoundationCodecService& operator=(MediaFoundationCodecService&&) noexcept;

  [[nodiscard]] bool available() const noexcept override;
  [[nodiscard]] std::string backend_name() const override;
  [[nodiscard]] std::string backend_error() const override;
  [[nodiscard]] std::vector<CodecCapability> capabilities() const override;
  [[nodiscard]] std::optional<AudioMetadata> probe(
      const std::filesystem::path& path, std::string& error) const override;
  [[nodiscard]] std::unique_ptr<IAudioDecoder> open_decoder(
      const std::filesystem::path& path, std::string& error) const override;
  [[nodiscard]] std::unique_ptr<IAudioEncoder> open_encoder(
      const std::filesystem::path& path, const EncodeSettings& settings,
      std::string& error) const override;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace amt::codec
