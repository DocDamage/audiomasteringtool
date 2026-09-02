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
#include <utility>
#include <vector>

#include "amt/analysis/FileAnalyzer.h"
#include "amt/audio/AudioBuffer.h"
#include "amt/codec/AudioIO.h"
#include "amt/dsp/Processors.h"
#include "amt/mastering/Audition.h"
#include "amt/mastering/Planner.h"
#include "amt/mastering/ProcessingGraph.h"
#include "amt/playback/ComparisonTransport.h"

namespace {

double max_abs(const amt::audio::AudioBuffer& buffer) {
  double peak = 0.0;
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    for (const float sample : buffer.channel(channel)) {
      peak = std::max(peak, std::abs(static_cast<double>(sample)));
    }
  }
  return peak;
}

class ConstantDecoder final : public amt::codec::IAudioDecoder {
 public:
  explicit ConstantDecoder(const float value) : value_(value) {
    metadata_.frames = 2048;
    metadata_.sample_rate = 48000;
    metadata_.channels = 2;
    metadata_.bit_depth = 32;
    metadata_.seekable = true;
    metadata_.channel_layout = amt::codec::ChannelLayout::stereo;
    metadata_.container = amt::codec::AudioContainer::wav;
    metadata_.sample_format = amt::codec::AudioSampleFormat::float32;
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
    for (std::size_t channel = 0; channel < 2U; ++channel) {
      std::fill(output.channel(channel).begin(), output.channel(channel).end(), value_);
    }
    position_ += static_cast<std::int64_t>(frames_read);
    return true;
  }
 private:
  float value_{0.0F};
  amt::codec::AudioMetadata metadata_;
  std::int64_t position_{0};
};

class ComparisonCodec final : public amt::codec::ICodecService {
 public:
  bool available() const noexcept override { return true; }
  std::string backend_name() const override { return "comparison-memory"; }
  std::string backend_error() const override { return {}; }
  std::vector<amt::codec::CodecCapability> capabilities() const override { return {}; }
  std::optional<amt::codec::AudioMetadata> probe(
      const std::filesystem::path& path, std::string& error) const override {
    auto decoder = open_decoder(path, error);
    if (!decoder) return std::nullopt;
    return decoder->metadata();
  }
  std::unique_ptr<amt::codec::IAudioDecoder> open_decoder(
      const std::filesystem::path& path, std::string&) const override {
    const auto name = path.filename().string();
    if (name == "original.wav") return std::make_unique<ConstantDecoder>(0.10F);
    if (name == "a.wav") return std::make_unique<ConstantDecoder>(0.20F);
    return std::make_unique<ConstantDecoder>(0.30F);
  }
  std::unique_ptr<amt::codec::IAudioEncoder> open_encoder(
      const std::filesystem::path&, const amt::codec::EncodeSettings&,
      std::string& error) const override {
    error = "unused";
    return nullptr;
  }
};

class CaptureDevice final : public amt::playback::IAudioOutputDevice {
 public:
  bool open(amt::playback::AudioOutputConfig,
            amt::playback::AudioRenderCallback callback, std::string&) override {
    callback_ = std::move(callback);
    return true;
  }
  bool start(std::string&) override {
    running_ = true;
    while (running_) {
      amt::audio::AudioBuffer buffer;
      const auto frames = callback_(buffer, 256U);
      if (frames == 0U) {
        running_ = false;
        break;
      }
      if (buffer.channels() > 0U && frames > 0U) {
        last_sample_ = buffer.channel(0U)[frames - 1U];
      }
    }
    return true;
  }
  bool pause(std::string&) override { return true; }
  bool resume(std::string&) override { return true; }
  void stop() noexcept override { running_ = false; }
  bool running() const noexcept override { return running_; }
  std::string backend_name() const override { return "capture"; }
  float last_sample() const noexcept { return last_sample_; }
 private:
  amt::playback::AudioRenderCallback callback_;
  bool running_{false};
  float last_sample_{0.0F};
};

void test_processing_graph() {
  amt::audio::AudioBuffer buffer(2U, 4096U);
  for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
    const double value = 0.55 * std::sin(2.0 * std::numbers::pi * 440.0 *
                                        static_cast<double>(frame) / 48000.0);
    buffer.channel(0U)[frame] = static_cast<float>(value);
    buffer.channel(1U)[frame] = static_cast<float>(value);
  }

  amt::mastering::ProcessingGraph graph;
  graph.add({.id = "gain", .bypass = false, .params = amt::dsp::GainParams{.gain_db = 9.0}});
  graph.add({.id = "clip", .bypass = false,
             .params = amt::dsp::ClipperParams{.threshold_db = -3.0, .softness = 0.2}});
  graph.add({.id = "limit", .bypass = false,
             .params = amt::dsp::LimiterParams{.ceiling_db = -3.0, .release_ms = 80.0}});
  std::string error;
  assert(graph.validate(error));
  assert(graph.to_json().find("\"type\":\"limiter\"") != std::string::npos);

  amt::mastering::ProcessingGraphRuntime runtime(graph, 48000, 2U);
  runtime.process(buffer);
  assert(max_abs(buffer) <= std::pow(10.0, -3.0 / 20.0) + 1.0e-5);

  amt::mastering::ProcessingGraph duplicate;
  duplicate.add({.id = "x", .params = amt::dsp::GainParams{}});
  duplicate.add({.id = "x", .params = amt::dsp::GainParams{}});
  assert(!duplicate.validate(error));
}

void test_planner_and_loudness_match() {
  amt::analysis::Phase1AnalysisReport report;
  report.metadata.sample_rate = 48000;
  report.metadata.channels = 2;
  report.loudness.integrated_lufs = -15.5;
  report.loudness.true_peak_dbtp = -2.0;
  report.loudness.crest_factor_db = 11.5;
  report.loudness.peak_to_loudness_ratio_db = 11.0;
  report.loudness.short_term_variation_lu = 6.5;
  report.spectrum.centroid_hz = 2800.0;
  report.spectrum.bands = {
      {.low_hz = 20.0, .high_hz = 60.0, .energy_ratio = 0.12},
      {.low_hz = 60.0, .high_hz = 250.0, .energy_ratio = 0.38},
      {.low_hz = 250.0, .high_hz = 500.0, .energy_ratio = 0.08},
      {.low_hz = 500.0, .high_hz = 2000.0, .energy_ratio = 0.20},
      {.low_hz = 2000.0, .high_hz = 6000.0, .energy_ratio = 0.16},
      {.low_hz = 6000.0, .high_hz = 20000.0, .energy_ratio = 0.06}};
  report.stereo.correlation = 0.82;
  report.stereo.low_band_width = 0.24;
  report.stereo.mid_band_width = 0.22;
  report.integrity.clipped_samples = 0;

  std::string error;
  const auto plan = amt::mastering::plan_mastering(report);
  assert(plan.master_a.recommended);
  assert(!plan.master_b.recommended);
  assert(plan.master_a.graph.validate(error));
  assert(plan.master_b.graph.validate(error));
  assert(plan.master_a.graph.to_json() != plan.master_b.graph.to_json());
  assert(plan.master_b.preservation_bias > plan.master_a.preservation_bias);
  assert(plan.master_b.target_lufs <= plan.master_a.target_lufs);

  auto controlled = plan;
  auto controls = amt::mastering::mastering_style_preset(
      amt::mastering::MasteringStyle::warm);
  controls.width = 1.12;
  controls.punch = 0.8;
  controls.stem_mix.drums_db = 2.5;
  controls.stem_mix.vocals_db = -3.0;
  amt::mastering::apply_mastering_controls(controlled, controls);
  assert(std::abs(controlled.master_a.target_lufs - controls.target_lufs) < 1.0e-9);
  assert(controlled.master_b.target_lufs < controlled.master_a.target_lufs);
  assert(controlled.master_a.graph.contains("a_user_tone"));
  assert(controlled.master_a.graph.contains("a_user_punch"));
  assert(controlled.master_a.graph.contains("a_user_width"));
  assert(controlled.master_a.graph.contains("a_user_warmth"));
  assert(std::abs(controlled.stem_mix.drums_db - 2.5) < 1.0e-9);
  assert(std::abs(controlled.stem_mix.vocals_db + 3.0) < 1.0e-9);
  assert(controlled.master_a.graph.validate(error));
  const auto& controlled_nodes = controlled.master_a.graph.nodes();
  const auto user_width = std::find_if(
      controlled_nodes.begin(), controlled_nodes.end(),
      [](const auto& node) { return node.id == "a_user_width"; });
  const auto limiter = std::find_if(
      controlled_nodes.begin(), controlled_nodes.end(),
      [](const auto& node) { return node.id == "a_limiter"; });
  assert(user_width != controlled_nodes.end());
  assert(limiter != controlled_nodes.end());
  assert(user_width < limiter);

  amt::analysis::LoudnessMetrics original;
  amt::analysis::LoudnessMetrics a;
  amt::analysis::LoudnessMetrics b;
  original.integrated_lufs = -15.0;
  a.integrated_lufs = -9.5;
  b.integrated_lufs = -11.5;
  const auto audition = amt::mastering::make_loudness_match_profile(original, a, b);
  assert(std::abs(audition.reference_lufs + 15.0) < 1.0e-9);
  assert(std::abs(audition.original_gain_db) < 1.0e-9);
  assert(audition.master_a_gain_db < audition.master_b_gain_db);
  assert(audition.master_a_gain_db <= 0.0 && audition.master_b_gain_db <= 0.0);
}

void test_synchronized_comparison_transport() {
  ComparisonCodec codecs;
  auto device = std::make_unique<CaptureDevice>();
  auto* capture = device.get();
  amt::playback::ComparisonTransport transport(codecs, std::move(device));
  std::string error;
  assert(transport.load({.path = "original.wav", .audition_gain_db = 0.0},
                        {.path = "a.wav", .audition_gain_db = 0.0},
                        {.path = "b.wav", .audition_gain_db = 0.0}, error));
  transport.select(amt::playback::ComparisonSource::master_b);
  assert(transport.play(error));
  assert(transport.state() == amt::playback::TransportState::finished);
  assert(transport.playhead_frame() == 2048);
  assert(std::abs(capture->last_sample() - 0.30F) < 1.0e-4F);
  assert(transport.seek(512, error));
  assert(transport.playhead_frame() == 512);
}

}  // namespace

int main() {
  test_processing_graph();
  test_planner_and_loudness_match();
  test_synchronized_comparison_transport();
  return 0;
}
