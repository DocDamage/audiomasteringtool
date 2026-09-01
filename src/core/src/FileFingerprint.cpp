#include "amt/core/FileFingerprint.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace amt::core {
namespace {

constexpr std::size_t kSha256BlockBytes = 64U;
constexpr std::size_t kReadBufferBytes = 1024U * 1024U;

constexpr std::array<std::uint32_t, 64U> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr std::uint32_t rotate_right(const std::uint32_t value,
                                     const unsigned int bits) noexcept {
  return (value >> bits) | (value << (32U - bits));
}

class Sha256 final {
 public:
  Sha256() = default;

  void update(const std::uint8_t* data, std::size_t size) {
    total_bytes_ += static_cast<std::uint64_t>(size);
    while (size > 0U) {
      const std::size_t copy_count = std::min(size, kSha256BlockBytes - buffer_size_);
      std::copy_n(data, copy_count, buffer_.data() + buffer_size_);
      buffer_size_ += copy_count;
      data += copy_count;
      size -= copy_count;
      if (buffer_size_ == kSha256BlockBytes) {
        transform(buffer_.data());
        buffer_size_ = 0U;
      }
    }
  }

  [[nodiscard]] std::string finalize_hex() {
    const std::uint64_t bit_length = total_bytes_ * 8U;
    buffer_[buffer_size_++] = 0x80U;

    if (buffer_size_ > 56U) {
      std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.end(),
                std::uint8_t{0U});
      transform(buffer_.data());
      buffer_size_ = 0U;
    }

    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
              buffer_.begin() + 56, std::uint8_t{0U});
    for (std::size_t index = 0U; index < 8U; ++index) {
      buffer_[63U - index] = static_cast<std::uint8_t>(bit_length >> (index * 8U));
    }
    transform(buffer_.data());
    buffer_size_ = 0U;

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto word : state_) output << std::setw(8) << word;
    return output.str();
  }

 private:
  void transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
      const std::size_t offset = index * 4U;
      words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                     (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                     (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                     static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const std::uint32_t small_sigma0 = rotate_right(words[index - 15U], 7U) ^
                                         rotate_right(words[index - 15U], 18U) ^
                                         (words[index - 15U] >> 3U);
      const std::uint32_t small_sigma1 = rotate_right(words[index - 2U], 17U) ^
                                         rotate_right(words[index - 2U], 19U) ^
                                         (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + small_sigma0 + words[index - 7U] + small_sigma1;
    }

    std::uint32_t a = state_[0U];
    std::uint32_t b = state_[1U];
    std::uint32_t c = state_[2U];
    std::uint32_t d = state_[3U];
    std::uint32_t e = state_[4U];
    std::uint32_t f = state_[5U];
    std::uint32_t g = state_[6U];
    std::uint32_t h = state_[7U];

    for (std::size_t index = 0U; index < words.size(); ++index) {
      const std::uint32_t big_sigma1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^
                                       rotate_right(e, 25U);
      const std::uint32_t choose = (e & f) ^ ((~e) & g);
      const std::uint32_t temporary1 = h + big_sigma1 + choose +
                                       kRoundConstants[index] + words[index];
      const std::uint32_t big_sigma0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^
                                       rotate_right(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temporary2 = big_sigma0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }

    state_[0U] += a;
    state_[1U] += b;
    state_[2U] += c;
    state_[3U] += d;
    state_[4U] += e;
    state_[5U] += f;
    state_[6U] += g;
    state_[7U] += h;
  }

  std::array<std::uint32_t, 8U> state_{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<std::uint8_t, kSha256BlockBytes> buffer_{};
  std::size_t buffer_size_{0U};
  std::uint64_t total_bytes_{0U};
};

}  // namespace

std::optional<FileFingerprint> fingerprint_file_sha256(
    const std::filesystem::path& path,
    std::string& error,
    const CancellationToken* cancellation,
    const ProgressCallback& progress) {
  error.clear();
  if (path.empty()) {
    error = "cannot fingerprint an empty file path";
    return std::nullopt;
  }

  std::error_code metadata_error;
  const auto initial_size = std::filesystem::file_size(path, metadata_error);
  if (metadata_error) {
    error = "unable to inspect source size for fingerprinting: " + metadata_error.message();
    return std::nullopt;
  }
  const auto initial_write_time = std::filesystem::last_write_time(path, metadata_error);
  if (metadata_error) {
    error = "unable to inspect source timestamp for fingerprinting: " + metadata_error.message();
    return std::nullopt;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "unable to open source file for fingerprinting";
    return std::nullopt;
  }

  Sha256 sha256;
  std::vector<char> buffer(kReadBufferBytes);
  std::uintmax_t processed = 0U;
  while (input) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "source fingerprinting cancelled";
      return std::nullopt;
    }

    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0) {
      const auto bytes = static_cast<std::size_t>(count);
      sha256.update(reinterpret_cast<const std::uint8_t*>(buffer.data()), bytes);
      processed += static_cast<std::uintmax_t>(bytes);
      if (initial_size > 0U) {
        report_progress(progress, std::min(1.0, static_cast<double>(processed) /
                                                   static_cast<double>(initial_size)));
      }
    }
  }

  if (!input.eof()) {
    error = "failed while reading source file for fingerprinting";
    return std::nullopt;
  }

  metadata_error.clear();
  const auto final_size = std::filesystem::file_size(path, metadata_error);
  if (metadata_error) {
    error = "unable to recheck source size after fingerprinting: " + metadata_error.message();
    return std::nullopt;
  }
  const auto final_write_time = std::filesystem::last_write_time(path, metadata_error);
  if (metadata_error) {
    error = "unable to recheck source timestamp after fingerprinting: " + metadata_error.message();
    return std::nullopt;
  }
  if (processed != initial_size || final_size != initial_size ||
      final_write_time != initial_write_time) {
    error = "source file changed while its fingerprint was being calculated";
    return std::nullopt;
  }

  report_progress(progress, 1.0);
  return FileFingerprint{.size_bytes = processed, .sha256 = sha256.finalize_hex()};
}

}  // namespace amt::core
