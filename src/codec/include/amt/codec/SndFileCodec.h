#pragma once

#include <memory>

#include "amt/codec/AudioIO.h"

namespace amt::codec {

class SndFileCodecService final : public ICodecService {
 public:
  struct Impl;

  SndFileCodecService();
  ~SndFileCodecService() override;
  SndFileCodecService(const SndFileCodecService&) = delete;
  SndFileCodecService& operator=(const SndFileCodecService&) = delete;
  SndFileCodecService(SndFileCodecService&&) noexcept;
  SndFileCodecService& operator=(SndFileCodecService&&) noexcept;

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
