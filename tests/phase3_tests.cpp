#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "amt/analysis/CharacterAnalyzer.h"
#include "amt/analysis/DeepAnalysis.h"
#include "amt/analysis/IntegrityAnalyzer.h"
#include "amt/analysis/PerceptualAnalyzer.h"
#include "amt/analysis/StructuralAnalyzer.h"
#include "amt/audio/AudioBuffer.h"
#include "amt/codec/AudioIO.h"

namespace {

constexpr int kRate = 48000;
constexpr std::int64_t kFrames = 8LL * kRate;

float program_sample(const std::int64_t frame, const std::size_t channel) {
  const double time = static_cast<double>(frame) / static_cast<double>(kRate);
  const bool second_half = frame >= kFrames / 2;
  const double base_amplitude = second_half ? 0.42 : 0.08;
  double value = base_amplitude * std::sin(2.0 * std::numbers::pi * 220.0 * time);
  if (second_half) {
    value += 0.14 * std::sin(2.0 * std::numbers::pi * 3500.0 * time);
  }
  // Keep the synthetic beat aligned to the analyzer's 1024-sample feature grid so
  // this test measures tempo detection rather than half-time aliasing from frame drift.
  const std::int64_t beat_period = 23LL * 1024LL;
  const std::int64_t beat_phase = frame % beat_period;
  if (beat_phase < 64) {
    value += 0.72 * (1.0 - static_cast<double>(beat_phase) / 64.0);
  }
  if (channel == 1U && second_half) {
    value += 0.025 * std::sin(2.0 * std::numbers::pi * 700.0 * time + 0.7);
  }
  return static_cast<float>(std::clamp(value, -0.98, 0.98));
}

class ProgramDecoder final : public amt::codec::IAudioDecoder {
 public:
  ProgramDecoder() {
    metadata_.frames = kFrames;
    metadata_.sample_rate = kRate;
    metadata_.channels = 2;
    metadata_.bit_depth = 32;
    metadata_.seekable = true;
    metadata_.channel_layout = amt::codec::ChannelLayout::stereo;
    metadata_.container = amt::codec::AudioContainer::wav;
    metadata_.sample_format = amt::codec::AudioSampleFormat::float32;
    metadata_.container_name = "synthetic";
    metadata_.sample_format_name = "float32";
  }

  const amt::codec::AudioMetadata& metadata() const noexcept override { return metadata_; }
  std::int64_t tell() const noexcept override { return position_; }

  bool seek(const std::int64_t frame, std::string& error) override {
    if (frame < 0 || frame > metadata_.frames) {
      error = "invalid synthetic seek";
      return false;
    }
    position_ = frame;
    return true;
  }

  bool read(amt::audio::AudioBuffer& output, const std::size_t max_frames,
            std::size_t& frames_read, std::string& error,
            const amt::core::CancellationToken* cancellation = nullptr) override {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "synthetic read cancelled";
      return false;
    }
    frames_read = static_cast<std::size_t>(std::min<std::int64_t>(
        static_cast<std::int64_t>(max_frames), metadata_.frames - position_));
    output.resize(2U, frames_read);
    for (std::size_t frame = 0U; frame < frames_read; ++frame) {
      const auto absolute = position_ + static_cast<std::int64_t>(frame);
      output.channel(0U)[frame] = program_sample(absolute, 0U);
      output.channel(1U)[frame] = program_sample(absolute, 1U);
    }
    position_ += static_cast<std::int64_t>(frames_read);
    return true;
  }

 private:
  amt::codec::AudioMetadata metadata_;
  std::int64_t position_{0};
};

class ProgramCodec final : public amt::codec::ICodecService {
 public:
  bool available() const noexcept override { return true; }
  std::string backend_name() const override { return "synthetic-program"; }
  std::string backend_error() const override { return {}; }
  std::vector<amt::codec::CodecCapability> capabilities() const override { return {}; }
  std::optional<amt::codec::AudioMetadata> probe(
      const std::filesystem::path&, std::string&) const override {
    ProgramDecoder decoder;
    return decoder.metadata();
  }
  std::unique_ptr<amt::codec::IAudioDecoder> open_decoder(
      const std::filesystem::path&, std::string&) const override {
    return std::make_unique<ProgramDecoder>();
  }
  std::unique_ptr<amt::codec::IAudioEncoder> open_encoder(
      const std::filesystem::path&, const amt::codec::EncodeSettings&,
      std::string& error) const override {
    error = "synthetic encoder unused";
    return nullptr;
  }
};

void feed_program(amt::analysis::StructuralAnalyzer& structural,
                  amt::analysis::PerceptualAnalyzer& perceptual) {
  constexpr std::size_t block_frames = 4096U;
  std::int64_t position = 0;
  while (position < kFrames) {
    const std::size_t count = static_cast<std::size_t>(std::min<std::int64_t>(
        block_frames, kFrames - position));
    amt::audio::AudioBuffer block(2U, count);
    for (std::size_t frame = 0U; frame < count; ++frame) {
      const auto absolute = position + static_cast<std::int64_t>(frame);
      block.channel(0U)[frame] = program_sample(absolute, 0U);
      block.channel(1U)[frame] = program_sample(absolute, 1U);
    }
    structural.process(block);
    perceptual.process(block);
    position += static_cast<std::int64_t>(count);
  }
}

void test_structural_and_perceptual() {
  amt::analysis::StructuralAnalyzer structural(kRate, 2U);
  amt::analysis::PerceptualAnalyzer perceptual(kRate, 2U);
  feed_program(structural, perceptual);

  const auto structure = structural.finalize();
  assert(structure.tempo.bpm >= 112.0 && structure.tempo.bpm <= 128.0);
  assert(structure.tempo.confidence > 0.30);
  assert(structure.tempo.onset_density_per_second > 1.0);
  assert(structure.sections.size() >= 2U);
  assert(structure.macro_dynamics.section_contrast_db > 5.0);

  const auto perception = perceptual.finalize();
  assert(perception.harshness_score > 0.25);
  const bool has_upper_mid_resonance = std::any_of(
      perception.resonances.begin(), perception.resonances.end(),
      [](const amt::analysis::ResonanceCandidate& candidate) {
        return candidate.frequency_hz > 2500.0 && candidate.frequency_hz < 5000.0;
      });
  assert(has_upper_mid_resonance);
}

void test_character_detection() {
  constexpr std::size_t frames = static_cast<std::size_t>(3 * kRate);
  amt::audio::AudioBuffer clipped(2U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double raw = 1.45 * std::sin(2.0 * std::numbers::pi * 80.0 *
                                      static_cast<double>(frame) / kRate);
    const float sample = static_cast<float>(std::clamp(raw, -1.0, 1.0));
    clipped.channel(0U)[frame] = sample;
    clipped.channel(1U)[frame] = sample;
  }
  amt::analysis::IntegrityAnalyzer integrity(kRate, 2U);
  amt::analysis::CharacterAnalyzer character(kRate, 2U);
  integrity.process(clipped);
  character.process(clipped);
  const auto integrity_metrics = integrity.finalize();
  const auto character_metrics = character.finalize(integrity_metrics);
  assert(integrity_metrics.clipped_samples > 0U);
  assert(character_metrics.hard_clip_likelihood > 0.50);
  assert(character_metrics.clipping_window_fraction > 0.50);
  assert(character_metrics.inference_confidence > 0.20);
}

void test_deep_report() {
  ProgramCodec codecs;
  std::string error;
  const auto report = amt::analysis::analyze_track(codecs, "program.wav", error);
  assert(report.has_value());
  assert(report->schema_version == 2);
  assert(report->structural.sections.size() >= 2U);
  assert(report->mix_health.dimensions.size() == 5U);
  assert(report->mix_health.overall_heuristic_score >= 0.0 &&
         report->mix_health.overall_heuristic_score <= 100.0);
  assert(!report->findings.empty());
  const auto json = amt::analysis::analysis_report_to_json(*report);
  assert(json.find("\"schema_version\":2") != std::string::npos);
  assert(json.find("\"mix_health\"") != std::string::npos);
  assert(json.find("\"findings\"") != std::string::npos);
}

}  // namespace

int main() {
  test_structural_and_perceptual();
  test_character_detection();
  test_deep_report();
  return 0;
}
