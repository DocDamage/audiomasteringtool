#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "amt/analysis/IntegrityAnalyzer.h"
#include "amt/analysis/LoudnessMeter.h"
#include "amt/analysis/SpectrumAnalyzer.h"
#include "amt/analysis/StereoAnalyzer.h"
#include "amt/audio/AudioBuffer.h"
#include "amt/audio/Resampler.h"
#include "amt/audio/WaveformCache.h"
#include "amt/codec/AudioIO.h"
#include "amt/core/JobControl.h"
#include "amt/playback/Transport.h"

namespace {

constexpr int kRate = 48000;

void expect_near(const double actual, const double expected, const double tolerance) {
  assert(std::abs(actual - expected) <= tolerance);
}

float sine_sample(const std::int64_t frame, const double frequency, const double amplitude,
                  const double phase = 0.0) {
  return static_cast<float>(amplitude * std::sin(
      2.0 * std::numbers::pi * frequency * static_cast<double>(frame) /
          static_cast<double>(kRate) + phase));
}

void test_audio_buffer_and_waveform() {
  std::vector<float> interleaved = {1.0F, -1.0F, 0.5F, -0.5F, 0.25F, -0.25F};
  auto buffer = amt::audio::AudioBuffer::from_interleaved(interleaved, 2U);
  assert(buffer.channels() == 2U && buffer.frames() == 3U);
  std::vector<float> roundtrip;
  buffer.to_interleaved(roundtrip);
  assert(roundtrip == interleaved);

  amt::audio::AudioBuffer waveform_buffer(1U, 16U);
  for (std::size_t i = 0; i < waveform_buffer.frames(); ++i) {
    waveform_buffer.channel(0U)[i] = static_cast<float>(i) / 15.0F;
  }
  amt::audio::WaveformPeakAccumulator accumulator(48000, 1U, 4U, 2U);
  accumulator.append(waveform_buffer);
  const auto cache = accumulator.finalize();
  assert(cache.source_frames == 16);
  assert(cache.levels.size() == 3U);
  assert(cache.levels[0].channels[0].size() == 4U);
  assert(cache.levels[1].channels[0].size() == 2U);
  assert(cache.levels[2].channels[0].size() == 1U);
  expect_near(cache.levels[0].channels[0].front().minimum, 0.0, 1.0e-6);
  expect_near(cache.levels[0].channels[0].front().maximum, 0.2, 1.0e-5);
}

void test_resampler() {
  auto resampler = amt::audio::make_high_quality_resampler(
      {.input_sample_rate = 48000, .output_sample_rate = 44100, .channels = 1U});
  std::int64_t input_frame = 0;
  std::int64_t output_frames = 0;
  for (const std::size_t block_size : {701U, 1024U, 1333U, 1742U}) {
    amt::audio::AudioBuffer block(1U, block_size);
    for (std::size_t frame = 0; frame < block_size; ++frame) {
      block.channel(0U)[frame] = sine_sample(input_frame++, 1000.0, 0.5);
    }
    output_frames += static_cast<std::int64_t>(resampler->process(block, false).frames());
  }
  assert(input_frame == 4800);
  amt::audio::AudioBuffer empty(1U, 0U);
  output_frames += static_cast<std::int64_t>(resampler->process(empty, true).frames());
  assert(output_frames == 4410);

  auto downsampler = amt::audio::make_high_quality_resampler(
      {.input_sample_rate = 48000, .output_sample_rate = 32000, .channels = 1U});
  amt::audio::AudioBuffer high_frequency(1U, 4800U);
  for (std::size_t frame = 0; frame < high_frequency.frames(); ++frame) {
    high_frequency.channel(0U)[frame] = sine_sample(static_cast<std::int64_t>(frame), 20000.0, 0.8);
  }
  auto first = downsampler->process(high_frequency, false);
  auto tail = downsampler->process(empty, true);
  double sum_squares = 0.0;
  std::uint64_t count = 0U;
  for (const auto* block : {&first, &tail}) {
    for (const float sample : block->channel(0U)) {
      sum_squares += static_cast<double>(sample) * sample;
      ++count;
    }
  }
  const double rms = count > 0U ? std::sqrt(sum_squares / static_cast<double>(count)) : 0.0;
  assert(rms < 0.08);
}

void test_loudness_conformance() {
  const double amplitude = std::pow(10.0, -23.0 / 20.0);
  amt::analysis::LoudnessMeter meter(kRate, 2U);
  const std::int64_t total_frames = 20LL * kRate;
  std::int64_t position = 0;
  while (position < total_frames) {
    const auto count = static_cast<std::size_t>(
        std::min<std::int64_t>(4096, total_frames - position));
    amt::audio::AudioBuffer block(2U, count);
    for (std::size_t frame = 0; frame < count; ++frame) {
      const float sample = sine_sample(position + static_cast<std::int64_t>(frame), 1000.0, amplitude);
      block.channel(0U)[frame] = sample;
      block.channel(1U)[frame] = sample;
    }
    meter.process(block);
    position += static_cast<std::int64_t>(count);
  }
  const auto result = meter.finalize();
  expect_near(result.integrated_lufs, -23.0, 0.1);
  expect_near(result.max_momentary_lufs, -23.0, 0.1);
  expect_near(result.max_short_term_lufs, -23.0, 0.1);

  // EBU Tech 3341 Test 6: conventional 5.0 channel mapping.
  amt::analysis::LoudnessMeter five_channel(kRate, 5U);
  position = 0;
  while (position < total_frames) {
    const auto count = static_cast<std::size_t>(
        std::min<std::int64_t>(4096, total_frames - position));
    amt::audio::AudioBuffer block(5U, count);
    const double gains[5] = {std::pow(10.0, -28.0 / 20.0), std::pow(10.0, -28.0 / 20.0),
                             std::pow(10.0, -24.0 / 20.0), std::pow(10.0, -30.0 / 20.0),
                             std::pow(10.0, -30.0 / 20.0)};
    for (std::size_t frame = 0; frame < count; ++frame) {
      const auto absolute_frame = position + static_cast<std::int64_t>(frame);
      for (std::size_t channel = 0; channel < 5U; ++channel) {
        block.channel(channel)[frame] = sine_sample(absolute_frame, 1000.0, gains[channel]);
      }
    }
    five_channel.process(block);
    position += static_cast<std::int64_t>(count);
  }
  expect_near(five_channel.finalize().integrated_lufs, -23.0, 0.1);
}

void test_true_peak_conformance() {
  amt::analysis::LoudnessMeter meter(kRate, 2U);
  const std::int64_t total_frames = 2LL * kRate;
  std::int64_t position = 0;
  while (position < total_frames) {
    const auto count = static_cast<std::size_t>(
        std::min<std::int64_t>(4096, total_frames - position));
    amt::audio::AudioBuffer block(2U, count);
    for (std::size_t frame = 0; frame < count; ++frame) {
      const float sample = sine_sample(position + static_cast<std::int64_t>(frame),
                                       kRate / 4.0, 1.41, std::numbers::pi / 4.0);
      block.channel(0U)[frame] = sample;
      block.channel(1U)[frame] = sample;
    }
    meter.process(block);
    position += static_cast<std::int64_t>(count);
  }
  const auto result = meter.finalize();
  assert(result.sample_peak_dbfs < 0.2);
  assert(result.true_peak_dbtp >= 2.6 && result.true_peak_dbtp <= 3.2);
}

void test_spectrum_stereo_and_integrity() {
  amt::audio::AudioBuffer mono(1U, 48000U);
  for (std::size_t frame = 0; frame < mono.frames(); ++frame) {
    mono.channel(0U)[frame] = sine_sample(static_cast<std::int64_t>(frame), 1000.0, 0.5);
  }
  amt::analysis::SpectrumAnalyzer spectrum(kRate, 1U);
  spectrum.process(mono);
  const auto spectrum_result = spectrum.finalize();
  assert(std::abs(spectrum_result.centroid_hz - 1000.0) < 30.0);
  assert(std::abs(spectrum_result.rolloff_85_hz - 1000.0) < 40.0);

  amt::audio::AudioBuffer stereo(2U, 48000U);
  amt::audio::AudioBuffer opposite(2U, 48000U);
  for (std::size_t frame = 0; frame < stereo.frames(); ++frame) {
    const float sample = sine_sample(static_cast<std::int64_t>(frame), 440.0, 0.5);
    stereo.channel(0U)[frame] = sample;
    stereo.channel(1U)[frame] = sample;
    opposite.channel(0U)[frame] = sample;
    opposite.channel(1U)[frame] = -sample;
  }
  amt::analysis::StereoAnalyzer centered(kRate, 2U);
  centered.process(stereo);
  const auto centered_result = centered.finalize();
  assert(centered_result.correlation > 0.999);
  assert(centered_result.low_band_width < 1.0e-6);
  expect_near(centered_result.mono_fold_down_delta_db, 0.0, 1.0e-6);

  amt::analysis::StereoAnalyzer anti_phase(kRate, 2U);
  anti_phase.process(opposite);
  const auto anti_phase_result = anti_phase.finalize();
  assert(anti_phase_result.correlation < -0.999);
  assert(anti_phase_result.mono_fold_down_delta_db < -100.0);
  assert(anti_phase_result.negative_correlation_window_fraction > 0.9);

  amt::audio::AudioBuffer damaged(2U, 8U);
  for (std::size_t frame = 0; frame < damaged.frames(); ++frame) {
    damaged.channel(0U)[frame] = 0.1F;
    damaged.channel(1U)[frame] = 0.2F;
  }
  damaged.channel(0U)[1] = std::numeric_limits<float>::quiet_NaN();
  damaged.channel(1U)[2] = std::numeric_limits<float>::infinity();
  damaged.channel(0U)[3] = 1.2F;
  damaged.channel(1U)[4] = 1.0F;
  damaged.channel(1U)[5] = 1.0F;
  damaged.channel(1U)[6] = 1.0F;
  amt::analysis::IntegrityAnalyzer integrity(kRate, 2U);
  integrity.process(damaged);
  const auto integrity_result = integrity.finalize();
  assert(integrity_result.nan_samples == 1U);
  assert(integrity_result.infinite_samples == 1U);
  assert(integrity_result.clipped_samples >= 4U);
  assert(integrity_result.repeated_full_scale_runs >= 1U);
  assert(integrity_result.longest_full_scale_run >= 3U);
}

class MemoryDecoder final : public amt::codec::IAudioDecoder {
 public:
  MemoryDecoder() {
    metadata_.frames = 100;
    metadata_.sample_rate = 48000;
    metadata_.channels = 2;
    metadata_.bit_depth = 32;
    metadata_.seekable = true;
    metadata_.channel_layout = amt::codec::ChannelLayout::stereo;
    metadata_.container = amt::codec::AudioContainer::wav;
    metadata_.sample_format = amt::codec::AudioSampleFormat::float32;
    metadata_.container_name = "memory";
    metadata_.sample_format_name = "float32";
  }
  const amt::codec::AudioMetadata& metadata() const noexcept override { return metadata_; }
  std::int64_t tell() const noexcept override { return position_; }
  bool seek(const std::int64_t frame, std::string& error) override {
    if (frame < 0 || frame > metadata_.frames) {
      error = "bad seek";
      return false;
    }
    position_ = frame;
    return true;
  }
  bool read(amt::audio::AudioBuffer& output, const std::size_t max_frames,
            std::size_t& frames_read, std::string&,
            const amt::core::CancellationToken* cancellation = nullptr) override {
    if (cancellation != nullptr && cancellation->is_cancelled()) return false;
    frames_read = static_cast<std::size_t>(std::min<std::int64_t>(
        static_cast<std::int64_t>(max_frames), metadata_.frames - position_));
    output.resize(2U, frames_read);
    position_ += static_cast<std::int64_t>(frames_read);
    return true;
  }
 private:
  amt::codec::AudioMetadata metadata_;
  std::int64_t position_{0};
};

class FakeCodec final : public amt::codec::ICodecService {
 public:
  bool available() const noexcept override { return true; }
  std::string backend_name() const override { return "fake"; }
  std::string backend_error() const override { return {}; }
  std::vector<amt::codec::CodecCapability> capabilities() const override { return {}; }
  std::optional<amt::codec::AudioMetadata> probe(
      const std::filesystem::path&, std::string&) const override {
    MemoryDecoder decoder;
    return decoder.metadata();
  }
  std::unique_ptr<amt::codec::IAudioDecoder> open_decoder(
      const std::filesystem::path&, std::string&) const override {
    return std::make_unique<MemoryDecoder>();
  }
  std::unique_ptr<amt::codec::IAudioEncoder> open_encoder(
      const std::filesystem::path&, const amt::codec::EncodeSettings&,
      std::string& error) const override {
    error = "unused";
    return nullptr;
  }
};

class FakeDevice final : public amt::playback::IAudioOutputDevice {
 public:
  bool open(amt::playback::AudioOutputConfig config,
            amt::playback::AudioRenderCallback callback, std::string&) override {
    config_ = config;
    callback_ = std::move(callback);
    return true;
  }
  bool start(std::string&) override {
    running_ = true;
    while (running_) {
      amt::audio::AudioBuffer buffer;
      const auto rendered = callback_(buffer, 32U);
      captured_frames_ += rendered;
      if (rendered == 0U) running_ = false;
    }
    return true;
  }
  bool pause(std::string&) override { return true; }
  bool resume(std::string&) override { return true; }
  void stop() noexcept override { running_ = false; }
  bool running() const noexcept override { return running_; }
  std::string backend_name() const override { return "fake output"; }
 private:
  amt::playback::AudioOutputConfig config_;
  amt::playback::AudioRenderCallback callback_;
  bool running_{false};
  std::size_t captured_frames_{0U};
};

void test_transport_and_cancellation() {
  amt::core::CancellationToken cancellation;
  assert(!cancellation.is_cancelled());
  cancellation.cancel();
  assert(cancellation.is_cancelled());

  FakeCodec codecs;
  amt::playback::Transport transport(codecs, std::make_unique<FakeDevice>());
  std::string error;
  assert(transport.load("memory.wav", error));
  assert(transport.state() == amt::playback::TransportState::stopped);
  assert(transport.seek(20, error));
  assert(transport.playhead_frame() == 20);
  assert(transport.play(error));
  assert(transport.state() == amt::playback::TransportState::finished);
  assert(transport.playhead_frame() == 100);
}

}  // namespace

int main() {
  test_audio_buffer_and_waveform();
  test_resampler();
  test_loudness_conformance();
  test_true_peak_conformance();
  test_spectrum_stereo_and_integrity();
  test_transport_and_cancellation();
  return 0;
}
