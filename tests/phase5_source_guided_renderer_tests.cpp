#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/codec/AudioIO.h"
#include "amt/core/JobControl.h"
#include "amt/separation/SourceGuidedStereoExecutor.h"

namespace {

struct FakeCodecState {
  std::filesystem::path canonical_path;
  amt::audio::AudioBuffer source;
  amt::codec::AudioMetadata source_metadata;
  std::filesystem::path encoded_path;
  amt::codec::AudioMetadata encoded_metadata;
  std::vector<float> encoded_interleaved;
  bool encoder_opened{false};
};

class FakeDecoder final : public amt::codec::IAudioDecoder {
 public:
  explicit FakeDecoder(std::shared_ptr<FakeCodecState> state) : state_(std::move(state)) {}

  const amt::codec::AudioMetadata& metadata() const noexcept override {
    return state_->source_metadata;
  }

  std::int64_t tell() const noexcept override { return position_; }

  bool seek(const std::int64_t frame, std::string& error) override {
    if (frame < 0 || frame > state_->source_metadata.frames) {
      error = "invalid fake decoder seek";
      return false;
    }
    position_ = frame;
    return true;
  }

  bool read(amt::audio::AudioBuffer& output,
            const std::size_t max_frames,
            std::size_t& frames_read,
            std::string& error,
            const amt::core::CancellationToken* cancellation = nullptr) override {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "fake decoder cancelled";
      return false;
    }
    const auto remaining = state_->source_metadata.frames - position_;
    frames_read = static_cast<std::size_t>(std::min<std::int64_t>(
        remaining, static_cast<std::int64_t>(max_frames)));
    output.resize(static_cast<std::size_t>(state_->source_metadata.channels), frames_read);
    for (std::size_t channel = 0U; channel < output.channels(); ++channel) {
      for (std::size_t frame = 0U; frame < frames_read; ++frame) {
        output.channel(channel)[frame] = state_->source.channel(channel)[
            static_cast<std::size_t>(position_) + frame];
      }
    }
    position_ += static_cast<std::int64_t>(frames_read);
    return true;
  }

 private:
  std::shared_ptr<FakeCodecState> state_;
  std::int64_t position_{0};
};

class FakeEncoder final : public amt::codec::IAudioEncoder {
 public:
  FakeEncoder(std::shared_ptr<FakeCodecState> state,
              std::filesystem::path path,
              const amt::codec::EncodeSettings& settings)
      : state_(std::move(state)), path_(std::move(path)), settings_(settings),
        output_(path_, std::ios::binary | std::ios::trunc) {
    state_->encoded_path = path_;
    state_->encoded_interleaved.clear();
    state_->encoder_opened = true;
  }

  bool write(const amt::audio::AudioBuffer& input,
             std::string& error,
             const amt::core::CancellationToken* cancellation = nullptr) override {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "fake encoder cancelled";
      return false;
    }
    if (!output_ || input.channels() != static_cast<std::size_t>(settings_.channels)) {
      error = "invalid fake encoder state";
      return false;
    }
    std::vector<float> interleaved;
    input.to_interleaved(interleaved);
    state_->encoded_interleaved.insert(state_->encoded_interleaved.end(),
                                       interleaved.begin(), interleaved.end());
    output_.write(reinterpret_cast<const char*>(interleaved.data()),
                  static_cast<std::streamsize>(interleaved.size() * sizeof(float)));
    if (!output_) {
      error = "fake encoder write failed";
      return false;
    }
    frames_ += static_cast<std::int64_t>(input.frames());
    return true;
  }

  bool finalize(std::string& error) override {
    output_.flush();
    if (!output_) {
      error = "fake encoder finalize failed";
      return false;
    }
    output_.close();
    state_->encoded_metadata.frames = frames_;
    state_->encoded_metadata.sample_rate = settings_.sample_rate;
    state_->encoded_metadata.channels = settings_.channels;
    state_->encoded_metadata.bit_depth = 32;
    state_->encoded_metadata.seekable = true;
    state_->encoded_metadata.channel_layout =
        amt::codec::channel_layout_from_count(settings_.channels);
    state_->encoded_metadata.container = settings_.container;
    state_->encoded_metadata.sample_format = settings_.sample_format;
    state_->encoded_metadata.container_name = "fake-wav";
    state_->encoded_metadata.sample_format_name = "float32";
    state_->encoded_metadata.tags = settings_.tags;
    return true;
  }

 private:
  std::shared_ptr<FakeCodecState> state_;
  std::filesystem::path path_;
  amt::codec::EncodeSettings settings_;
  std::ofstream output_;
  std::int64_t frames_{0};
};

class FakeCodec final : public amt::codec::ICodecService {
 public:
  explicit FakeCodec(std::shared_ptr<FakeCodecState> state) : state_(std::move(state)) {}

  bool available() const noexcept override { return true; }
  std::string backend_name() const override { return "phase5-fake-codec"; }
  std::string backend_error() const override { return {}; }
  std::vector<amt::codec::CodecCapability> capabilities() const override { return {}; }

  std::optional<amt::codec::AudioMetadata> probe(
      const std::filesystem::path& path, std::string& error) const override {
    if (path == state_->canonical_path) return state_->source_metadata;
    if (path == state_->encoded_path && std::filesystem::is_regular_file(path)) {
      return state_->encoded_metadata;
    }
    error = "fake codec cannot probe path";
    return std::nullopt;
  }

  std::unique_ptr<amt::codec::IAudioDecoder> open_decoder(
      const std::filesystem::path& path, std::string& error) const override {
    if (path != state_->canonical_path) {
      error = "fake codec rejected decoder path";
      return nullptr;
    }
    return std::make_unique<FakeDecoder>(state_);
  }

  std::unique_ptr<amt::codec::IAudioEncoder> open_encoder(
      const std::filesystem::path& path,
      const amt::codec::EncodeSettings& settings,
      std::string& error) const override {
    if (settings.container != amt::codec::AudioContainer::wav ||
        settings.sample_format != amt::codec::AudioSampleFormat::float32 ||
        settings.sample_rate <= 0 || settings.channels <= 0) {
      error = "fake codec rejected encoder settings";
      return nullptr;
    }
    return std::make_unique<FakeEncoder>(state_, path, settings);
  }

 private:
  std::shared_ptr<FakeCodecState> state_;
};

std::filesystem::path test_root() {
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("amt-phase5-guided-renderer-" + std::to_string(ticks));
}

void write_sentinel(const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << "CANONICAL-SENTINEL";
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::shared_ptr<FakeCodecState> make_state(const std::filesystem::path& canonical,
                                           const std::size_t frames) {
  auto state = std::make_shared<FakeCodecState>();
  state->canonical_path = canonical;
  state->source.resize(2U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    state->source.channel(0U)[frame] = 1.0F;
    state->source.channel(1U)[frame] = 1.0F;
  }
  state->source_metadata.frames = static_cast<std::int64_t>(frames);
  state->source_metadata.sample_rate = 48000;
  state->source_metadata.channels = 2;
  state->source_metadata.bit_depth = 24;
  state->source_metadata.seekable = true;
  state->source_metadata.channel_layout = amt::codec::ChannelLayout::stereo;
  state->source_metadata.container = amt::codec::AudioContainer::wav;
  state->source_metadata.sample_format = amt::codec::AudioSampleFormat::pcm24;
  state->source_metadata.container_name = "fake-source";
  state->source_metadata.sample_format_name = "pcm24";
  state->source_metadata.tags["artist"] = "Phase 5";
  return state;
}

amt::separation::SourceGuidedControlPlan gain_plan(const std::size_t frames) {
  amt::separation::SourceGuidedControlPlan plan;
  plan.operates_on_canonical_stereo = true;
  plan.envelopes.push_back({.source = amt::separation::StemRole::bass,
                            .sample_rate = 48000,
                            .hop_frames = 1U,
                            .source_confidence = 1.0,
                            .activity = std::vector<float>(frames, 1.0F)});
  amt::separation::SourceGuidedIntervention intervention;
  intervention.source = amt::separation::StemRole::bass;
  intervention.action = amt::separation::SourceGuidedAction::gain_riding;
  intervention.amount = -1.5;
  intervention.confidence = 1.0;
  plan.bindings.push_back({.intervention = intervention, .envelope_index = 0U});
  return plan;
}

void test_render_preserves_canonical_and_publishes() {
  const auto root = test_root();
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "source.wav";
  const auto output = root / "guided.wav";
  write_sentinel(canonical);

  constexpr std::size_t frames = 64U;
  auto state = make_state(canonical, frames);
  FakeCodec codecs(state);
  std::string error;
  const auto result = amt::separation::render_source_guided_stereo(
      codecs, canonical, output, gain_plan(frames), error);
  assert(result.has_value());
  assert(error.empty());
  assert(result->canonical_program_path);
  assert(result->applied_bindings == 1U);
  assert(result->frames == static_cast<std::int64_t>(frames));
  assert(std::filesystem::is_regular_file(output));
  assert(read_file(canonical) == "CANONICAL-SENTINEL");
  assert(!std::filesystem::exists(output.string() + ".partial.wav"));
  assert(!std::filesystem::exists(output.string() + ".bak"));
  assert(state->encoded_interleaved.size() == frames * 2U);
  const float expected = static_cast<float>(std::pow(10.0, -1.5 / 20.0));
  for (const float sample : state->encoded_interleaved) {
    assert(std::abs(sample - expected) < 1.0e-5F);
  }
  std::filesystem::remove_all(root, ignored);
}

void test_rejects_canonical_output_and_sidecar_collisions() {
  const auto root = test_root();
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);

  {
    const auto canonical = root / "same.wav";
    write_sentinel(canonical);
    auto state = make_state(canonical, 8U);
    FakeCodec codecs(state);
    std::string error;
    const auto result = amt::separation::render_source_guided_stereo(
        codecs, canonical, canonical, gain_plan(8U), error);
    assert(!result.has_value());
    assert(!error.empty());
    assert(!state->encoder_opened);
    assert(read_file(canonical) == "CANONICAL-SENTINEL");
  }

  {
    const auto output = root / "sidecar.wav";
    const auto canonical = std::filesystem::path(output.string() + ".partial.wav");
    write_sentinel(canonical);
    auto state = make_state(canonical, 8U);
    FakeCodec codecs(state);
    std::string error;
    const auto result = amt::separation::render_source_guided_stereo(
        codecs, canonical, output, gain_plan(8U), error);
    assert(!result.has_value());
    assert(!error.empty());
    assert(!state->encoder_opened);
    assert(read_file(canonical) == "CANONICAL-SENTINEL");
  }

  {
    const auto output = root / "backup.wav";
    const auto canonical = std::filesystem::path(output.string() + ".bak");
    write_sentinel(canonical);
    auto state = make_state(canonical, 8U);
    FakeCodec codecs(state);
    std::string error;
    const auto result = amt::separation::render_source_guided_stereo(
        codecs, canonical, output, gain_plan(8U), error);
    assert(!result.has_value());
    assert(!error.empty());
    assert(!state->encoder_opened);
    assert(read_file(canonical) == "CANONICAL-SENTINEL");
  }

  std::filesystem::remove_all(root, ignored);
}

void test_cancellation_removes_partial_output() {
  const auto root = test_root();
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "source.wav";
  const auto output = root / "cancelled.wav";
  write_sentinel(canonical);

  constexpr std::size_t frames = 20000U;
  auto state = make_state(canonical, frames);
  FakeCodec codecs(state);
  amt::core::CancellationToken cancellation;
  std::string error;
  const auto result = amt::separation::render_source_guided_stereo(
      codecs, canonical, output, gain_plan(frames), error, {}, &cancellation,
      [&cancellation](const double value) {
        if (value > 0.0) cancellation.cancel();
      });
  assert(!result.has_value());
  assert(!error.empty());
  assert(cancellation.is_cancelled());
  assert(read_file(canonical) == "CANONICAL-SENTINEL");
  assert(!std::filesystem::exists(output));
  assert(!std::filesystem::exists(output.string() + ".partial.wav"));
  assert(!std::filesystem::exists(output.string() + ".bak"));
  std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main() {
  test_render_preserves_canonical_and_publishes();
  test_rejects_canonical_output_and_sidecar_collisions();
  test_cancellation_removes_partial_output();
  return 0;
}
