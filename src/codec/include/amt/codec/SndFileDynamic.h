#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace amt::codec {

struct AudioFileInfo {
  std::int64_t frames{0};
  int sample_rate{0};
  int channels{0};
  int format{0};
  std::string container;
  std::string subtype;
};

class SndFileRuntime {
 public:
  SndFileRuntime();
  ~SndFileRuntime();

  SndFileRuntime(const SndFileRuntime&) = delete;
  SndFileRuntime& operator=(const SndFileRuntime&) = delete;
  SndFileRuntime(SndFileRuntime&&) noexcept;
  SndFileRuntime& operator=(SndFileRuntime&&) noexcept;

  [[nodiscard]] bool available() const noexcept;
  [[nodiscard]] std::string load_error() const;

  [[nodiscard]] std::optional<AudioFileInfo> probe(
      const std::filesystem::path& path, std::string& error) const;

  [[nodiscard]] bool lossless_rerender(
      const std::filesystem::path& input,
      const std::filesystem::path& output,
      std::string& error) const;

  [[nodiscard]] bool verify_pcm_equal(
      const std::filesystem::path& a,
      const std::filesystem::path& b,
      std::string& error) const;

 private:
  struct Impl;
  Impl* impl_{nullptr};
};

[[nodiscard]] bool is_phase0_baseline_format(int format) noexcept;
[[nodiscard]] std::string describe_container(int format);
[[nodiscard]] std::string describe_subtype(int format);

}  // namespace amt::codec
