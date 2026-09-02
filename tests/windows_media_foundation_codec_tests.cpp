#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <string>

#include "amt/codec/AudioIO.h"
#include "amt/codec/SndFileCodec.h"

namespace {

void write_u16(std::ofstream& output, const std::uint16_t value) {
  const char bytes[2] = {
      static_cast<char>(value & 0xffU),
      static_cast<char>((value >> 8U) & 0xffU)};
  output.write(bytes, sizeof(bytes));
}

void write_u32(std::ofstream& output, const std::uint32_t value) {
  const char bytes[4] = {
      static_cast<char>(value & 0xffU),
      static_cast<char>((value >> 8U) & 0xffU),
      static_cast<char>((value >> 16U) & 0xffU),
      static_cast<char>((value >> 24U) & 0xffU)};
  output.write(bytes, sizeof(bytes));
}

void write_pcm_fixture(const std::filesystem::path& path) {
  constexpr std::uint32_t sample_rate = 44100U;
  constexpr std::uint16_t channels = 2U;
  constexpr std::uint16_t bits = 16U;
  constexpr std::uint32_t frames = sample_rate;
  constexpr std::uint32_t data_bytes = frames * channels * (bits / 8U);

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output);
  output.write("RIFF", 4);
  write_u32(output, 36U + data_bytes);
  output.write("WAVEfmt ", 8);
  write_u32(output, 16U);
  write_u16(output, 1U);
  write_u16(output, channels);
  write_u32(output, sample_rate);
  write_u32(output, sample_rate * channels * (bits / 8U));
  write_u16(output, channels * (bits / 8U));
  write_u16(output, bits);
  output.write("data", 4);
  write_u32(output, data_bytes);

  for (std::uint32_t frame = 0U; frame < frames; ++frame) {
    const double phase = 2.0 * std::numbers::pi * 440.0 *
                         static_cast<double>(frame) /
                         static_cast<double>(sample_rate);
    const auto sample = static_cast<std::int16_t>(
        std::lround(std::sin(phase) * 12000.0));
    write_u16(output, static_cast<std::uint16_t>(sample));
    write_u16(output, static_cast<std::uint16_t>(sample));
  }
  output.flush();
  assert(output);
}

void verify_lossy_export(amt::codec::SndFileCodecService& codecs,
                         const std::filesystem::path& input,
                         const std::filesystem::path& output,
                         const amt::codec::AudioContainer container) {
  amt::codec::ExportRequest request;
  request.container = container;
  request.sample_format = amt::codec::AudioSampleFormat::compressed;
  request.dither_when_reducing_integer_depth = false;

  std::string error;
  std::cerr << "exporting " << output.extension().string() << std::endl;
  assert(amt::codec::export_audio(codecs, input, output, request, error));
  std::cerr << "probing " << output.extension().string() << std::endl;
  assert(std::filesystem::is_regular_file(output));
  assert(std::filesystem::file_size(output) > 1024U);

  const auto metadata = codecs.probe(output, error);
  assert(metadata);
  assert(metadata->container == container);
  assert(metadata->sample_rate == 44100);
  assert(metadata->channels == 2);

  auto decoder = codecs.open_decoder(output, error);
  assert(decoder);
  std::cerr << "decoding " << output.extension().string() << std::endl;
  std::size_t total_frames = 0U;
  double energy = 0.0;
  while (true) {
    amt::audio::AudioBuffer block;
    std::size_t frames = 0U;
    assert(decoder->read(block, 4096U, frames, error));
    if (frames == 0U) break;
    total_frames += frames;
    for (const float sample : block.channel(0U)) {
      assert(std::isfinite(sample));
      energy += static_cast<double>(sample) * sample;
    }
  }
  std::cerr << "decoded frames=" << total_frames << " energy=" << energy
            << std::endl;
  assert(total_frames > 40000U);
  assert(energy > 1.0);
  std::cerr << "verified " << output.extension().string() << std::endl;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    "amt_windows_media_foundation_codec_test";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);

  const auto input = root / "input.wav";
  write_pcm_fixture(input);

  amt::codec::SndFileCodecService codecs;
  assert(codecs.available());
  bool mp3_supported = false;
  bool aac_supported = false;
  for (const auto& capability : codecs.capabilities()) {
    if (capability.container == amt::codec::AudioContainer::mp3) {
      mp3_supported = capability.decode && capability.encode;
    }
    if (capability.container == amt::codec::AudioContainer::aac_m4a) {
      aac_supported = capability.decode && capability.encode;
    }
  }
  assert(mp3_supported);
  assert(aac_supported);

  verify_lossy_export(codecs, input, root / "preview.mp3",
                      amt::codec::AudioContainer::mp3);
  verify_lossy_export(codecs, input, root / "preview.m4a",
                      amt::codec::AudioContainer::aac_m4a);

  std::filesystem::remove_all(root, ignored);
  return 0;
}
