#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "amt/analysis/FileAnalyzer.h"
#include "amt/audio/AudioBuffer.h"
#include "amt/codec/AudioIO.h"
#include "amt/core/JobControl.h"
#include "amt/mastering/Audition.h"
#include "amt/mastering/OfflineRenderer.h"
#include "amt/mastering/Planner.h"
#include "amt/mastering/SourceGuidedMastering.h"
#include "amt/separation/Separation.h"
#include "amt/separation/SourceGuidance.h"
#include "amt/separation/SourceGuidedProcessing.h"
#include "amt/separation/SourceIssueInference.h"

namespace amt::test {

struct MemoryAudioFile {
  amt::audio::AudioBuffer audio;
  amt::codec::AudioMetadata metadata;
};

struct MemoryCodecState {
  std::map<std::filesystem::path, MemoryAudioFile> files;
  std::vector<std::filesystem::path> decoder_paths;
  std::vector<std::filesystem::path> encoder_paths;
  std::filesystem::path last_guided_alias;
};

class MemoryDecoder final : public amt::codec::IAudioDecoder {
 public:
  explicit MemoryDecoder(MemoryAudioFile file) : file_(std::move(file)) {}

  const amt::codec::AudioMetadata& metadata() const noexcept override {
    return file_.metadata;
  }

  std::int64_t tell() const noexcept override { return position_; }

  bool seek(const std::int64_t frame, std::string& error) override {
    if (frame < 0 || frame > file_.metadata.frames) {
      error = "memory decoder seek is outside the source";
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
      error = "memory decoder cancelled";
      return false;
    }
    const auto remaining = file_.metadata.frames - position_;
    frames_read = static_cast<std::size_t>(std::min<std::int64_t>(
        remaining, static_cast<std::int64_t>(max_frames)));
    output.resize(static_cast<std::size_t>(file_.metadata.channels), frames_read);
    for (std::size_t channel = 0U; channel < output.channels(); ++channel) {
      const auto source = file_.audio.channel(channel);
      auto destination = output.channel(channel);
      const auto offset = static_cast<std::size_t>(position_);
      std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset),
                  static_cast<std::ptrdiff_t>(frames_read), destination.begin());
    }
    position_ += static_cast<std::int64_t>(frames_read);
    return true;
  }

 private:
  MemoryAudioFile file_;
  std::int64_t position_{0};
};

class MemoryEncoder final : public amt::codec::IAudioEncoder {
 public:
  MemoryEncoder(std::shared_ptr<MemoryCodecState> state,
                std::filesystem::path path,
                amt::codec::EncodeSettings settings)
      : state_(std::move(state)),
        path_(std::move(path)),
        settings_(std::move(settings)),
        channels_(static_cast<std::size_t>(settings_.channels)) {
    if (!path_.parent_path().empty()) {
      std::error_code ignored;
      std::filesystem::create_directories(path_.parent_path(), ignored);
    }
    output_.open(path_, std::ios::binary | std::ios::trunc);
    state_->encoder_paths.push_back(path_);
  }

  bool write(const amt::audio::AudioBuffer& input,
             std::string& error,
             const amt::core::CancellationToken* cancellation = nullptr) override {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "memory encoder cancelled";
      return false;
    }
    if (!output_ || input.channels() != channels_.size()) {
      error = "memory encoder received invalid audio";
      return false;
    }

    for (std::size_t channel = 0U; channel < channels_.size(); ++channel) {
      const auto source = input.channel(channel);
      channels_[channel].insert(channels_[channel].end(), source.begin(), source.end());
    }

    std::vector<float> interleaved;
    input.to_interleaved(interleaved);
    if (!interleaved.empty()) {
      output_.write(reinterpret_cast<const char*>(interleaved.data()),
                    static_cast<std::streamsize>(interleaved.size() * sizeof(float)));
    }
    if (!output_) {
      error = "memory encoder failed to write";
      return false;
    }
    return true;
  }

  bool finalize(std::string& error) override {
    if (!output_) {
      error = "memory encoder is not writable";
      return false;
    }
    output_.flush();
    if (!output_) {
      error = "memory encoder failed to finalize";
      return false;
    }
    output_.close();

    const std::size_t frames = channels_.empty() ? 0U : channels_.front().size();
    for (const auto& channel : channels_) {
      if (channel.size() != frames) {
        error = "memory encoder channel length mismatch";
        return false;
      }
    }

    MemoryAudioFile file;
    file.audio.resize(channels_.size(), frames);
    for (std::size_t channel = 0U; channel < channels_.size(); ++channel) {
      auto destination = file.audio.channel(channel);
      std::copy(channels_[channel].begin(), channels_[channel].end(), destination.begin());
    }
    file.metadata.frames = static_cast<std::int64_t>(frames);
    file.metadata.sample_rate = settings_.sample_rate;
    file.metadata.channels = settings_.channels;
    file.metadata.bit_depth =
        settings_.sample_format == amt::codec::AudioSampleFormat::float32
            ? 32
            : amt::codec::integer_bit_depth(settings_.sample_format);
    file.metadata.seekable = true;
    file.metadata.channel_layout =
        amt::codec::channel_layout_from_count(settings_.channels);
    file.metadata.container = settings_.container;
    file.metadata.sample_format = settings_.sample_format;
    file.metadata.container_name = "memory-wav";
    file.metadata.sample_format_name =
        settings_.sample_format == amt::codec::AudioSampleFormat::float32
            ? "float32"
            : "pcm";
    file.metadata.tags = settings_.tags;
    state_->files[path_] = file;

    constexpr char kPartialSuffix[] = ".partial.wav";
    const auto path_text = path_.string();
    const std::string suffix{kPartialSuffix};
    if (path_text.size() > suffix.size() &&
        path_text.compare(path_text.size() - suffix.size(), suffix.size(), suffix) == 0) {
      const auto alias = std::filesystem::path(
          path_text.substr(0U, path_text.size() - suffix.size()));
      state_->files[alias] = file;
      state_->last_guided_alias = alias;
    }
    return true;
  }

 private:
  std::shared_ptr<MemoryCodecState> state_;
  std::filesystem::path path_;
  amt::codec::EncodeSettings settings_;
  std::vector<std::vector<float>> channels_;
  std::ofstream output_;
};

class MemoryCodec final : public amt::codec::ICodecService {
 public:
  explicit MemoryCodec(std::shared_ptr<MemoryCodecState> state)
      : state_(std::move(state)) {}

  bool available() const noexcept override { return true; }
  std::string backend_name() const override { return "phase5-mastering-memory"; }
  std::string backend_error() const override { return {}; }
  std::vector<amt::codec::CodecCapability> capabilities() const override { return {}; }

  std::optional<amt::codec::AudioMetadata> probe(
      const std::filesystem::path& path, std::string& error) const override {
    const auto iterator = state_->files.find(path);
    if (iterator == state_->files.end()) {
      error = "memory codec cannot probe path: " + path.string();
      return std::nullopt;
    }
    return iterator->second.metadata;
  }

  std::unique_ptr<amt::codec::IAudioDecoder> open_decoder(
      const std::filesystem::path& path, std::string& error) const override {
    const auto iterator = state_->files.find(path);
    if (iterator == state_->files.end()) {
      error = "memory codec cannot decode path: " + path.string();
      return nullptr;
    }
    state_->decoder_paths.push_back(path);
    return std::make_unique<MemoryDecoder>(iterator->second);
  }

  std::unique_ptr<amt::codec::IAudioEncoder> open_encoder(
      const std::filesystem::path& path,
      const amt::codec::EncodeSettings& settings,
      std::string& error) const override {
    if (settings.sample_rate <= 0 || settings.channels <= 0 ||
        settings.container != amt::codec::AudioContainer::wav) {
      error = "memory codec rejected encoder settings";
      return nullptr;
    }
    return std::make_unique<MemoryEncoder>(state_, path, settings);
  }

 private:
  std::shared_ptr<MemoryCodecState> state_;
};

inline std::filesystem::path test_root(const std::string& name) {
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("amt-phase5-source-guided-mastering-" + name + "-" + std::to_string(ticks));
}

inline void write_text_file(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

inline std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

inline amt::codec::AudioMetadata metadata_for(const amt::audio::AudioBuffer& audio,
                                              const int sample_rate,
                                              const int bit_depth = 32) {
  amt::codec::AudioMetadata metadata;
  metadata.frames = static_cast<std::int64_t>(audio.frames());
  metadata.sample_rate = sample_rate;
  metadata.channels = static_cast<int>(audio.channels());
  metadata.bit_depth = bit_depth;
  metadata.seekable = true;
  metadata.channel_layout =
      amt::codec::channel_layout_from_count(static_cast<int>(audio.channels()));
  metadata.container = amt::codec::AudioContainer::wav;
  metadata.sample_format = amt::codec::AudioSampleFormat::float32;
  metadata.container_name = "memory-source";
  metadata.sample_format_name = "float32";
  return metadata;
}

inline amt::audio::AudioBuffer make_program(const std::size_t frames,
                                            const int sample_rate = 48000) {
  amt::audio::AudioBuffer audio(2U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double time = static_cast<double>(frame) / static_cast<double>(sample_rate);
    const double low = std::sin(2.0 * std::numbers::pi * 110.0 * time);
    const double mid = std::sin(2.0 * std::numbers::pi * 440.0 * time);
    const double high = std::sin(2.0 * std::numbers::pi * 3442.9056 * time);
    audio.channel(0U)[frame] =
        static_cast<float>(0.18 * low + 0.07 * mid + 0.025 * high);
    audio.channel(1U)[frame] =
        static_cast<float>(0.16 * low + 0.065 * std::sin(
            2.0 * std::numbers::pi * 440.0 * time + 0.19) + 0.02 * high);
  }
  return audio;
}

inline amt::audio::AudioBuffer make_bass_stem(const std::size_t frames,
                                              const int sample_rate = 48000) {
  amt::audio::AudioBuffer audio(1U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double time = static_cast<double>(frame) / static_cast<double>(sample_rate);
    audio.channel(0U)[frame] =
        static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * 55.0 * time));
  }
  return audio;
}

inline amt::audio::AudioBuffer make_kick_drum_stem(const std::size_t frames,
                                                   const int sample_rate = 48000) {
  amt::audio::AudioBuffer audio(1U, frames);
  const std::size_t period = static_cast<std::size_t>(sample_rate / 2);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const std::size_t local = frame % period;
    const double time = static_cast<double>(local) / static_cast<double>(sample_rate);
    if (time < 0.14) {
      const double envelope = std::exp(-time * 24.0);
      audio.channel(0U)[frame] = static_cast<float>(
          0.42 * envelope * std::sin(2.0 * std::numbers::pi * 68.0 * time));
    }
  }
  return audio;
}

inline amt::audio::AudioBuffer make_wide_bass_stem(const std::size_t frames,
                                                   const int sample_rate = 48000) {
  amt::audio::AudioBuffer audio(2U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double time = static_cast<double>(frame) / static_cast<double>(sample_rate);
    const float sample = static_cast<float>(
        0.35 * std::sin(2.0 * std::numbers::pi * 55.0 * time));
    audio.channel(0U)[frame] = sample;
    audio.channel(1U)[frame] = -sample;
  }
  return audio;
}

inline void seed_audio(const std::shared_ptr<MemoryCodecState>& state,
                       const std::filesystem::path& path,
                       amt::audio::AudioBuffer audio,
                       const int sample_rate = 48000) {
  MemoryAudioFile file;
  file.metadata = metadata_for(audio, sample_rate);
  file.audio = std::move(audio);
  state->files[path] = std::move(file);
}

inline amt::analysis::Phase1AnalysisReport analyze(
    MemoryCodec& codecs, const std::filesystem::path& path) {
  std::string error;
  const auto report = amt::analysis::analyze_file(codecs, path, error);
  assert(report.has_value());
  assert(error.empty());
  return *report;
}

inline amt::mastering::RenderSettings float_render_settings() {
  amt::mastering::RenderSettings settings;
  settings.sample_format = amt::codec::AudioSampleFormat::float32;
  settings.verify_output = true;
  return settings;
}

inline amt::separation::SourceGuidanceResult guidance_for(
    const std::filesystem::path& stem_path,
    const std::int64_t frames,
    const amt::separation::SeparationMode mode) {
  amt::separation::SourceGuidanceResult guidance;
  guidance.decision.mode = mode;
  guidance.decision.expected_benefit = 0.85;
  guidance.decision.artifact_risk = 0.10;
  guidance.decision.confidence = 1.0;

  amt::separation::SeparationResult separation;
  separation.sample_rate = 48000;
  separation.frames = frames;
  separation.overall_confidence = 1.0;
  separation.complete_reconstruction = false;
  separation.artifacts.push_back({
      .kind = amt::separation::CacheArtifactKind::stem_audio,
      .role = amt::separation::StemRole::bass,
      .path = stem_path,
      .confidence = 1.0});
  guidance.separation = std::move(separation);
  return guidance;
}

inline std::vector<amt::separation::SourceGuidedIssue> bass_level_issue() {
  return {{
      .source = amt::separation::StemRole::bass,
      .type = amt::separation::SourceGuidedIssueType::excessive_level,
      .severity = 1.0,
      .confidence = 1.0,
      .evidence = "Synthetic bass estimate is active for this integration fixture."}};
}

inline amt::audio::AudioBuffer make_harsh_tonal_stem(const std::size_t frames,
                                                     const int sample_rate = 48000) {
  amt::audio::AudioBuffer audio(2U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double time = static_cast<double>(frame) / static_cast<double>(sample_rate);
    const float sample = static_cast<float>(
        0.11 * std::sin(2.0 * std::numbers::pi * 3500.0 * time));
    audio.channel(0U)[frame] = sample;
    audio.channel(1U)[frame] = sample;
  }
  return audio;
}

inline amt::audio::AudioBuffer make_harsh_program(const std::size_t frames,
                                                  const int sample_rate = 48000) {
  amt::audio::AudioBuffer audio(2U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double time = static_cast<double>(frame) / static_cast<double>(sample_rate);
    const float sample = static_cast<float>(
        0.22 * std::sin(2.0 * std::numbers::pi * 3500.0 * time));
    audio.channel(0U)[frame] = sample;
    audio.channel(1U)[frame] = sample;
  }
  return audio;
}

inline amt::audio::AudioBuffer make_quiet_bass_stem(const std::size_t frames,
                                                    const int sample_rate = 48000) {
  amt::audio::AudioBuffer audio(1U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double time = static_cast<double>(frame) / static_cast<double>(sample_rate);
    audio.channel(0U)[frame] = static_cast<float>(
        0.025 * std::sin(2.0 * std::numbers::pi * 55.0 * time));
  }
  return audio;
}

inline void assert_audio_near(const MemoryCodecState& state,
                             const std::filesystem::path& first,
                             const std::filesystem::path& second,
                             const double tolerance = 1.0e-5) {
  const auto first_iterator = state.files.find(first);
  const auto second_iterator = state.files.find(second);
  assert(first_iterator != state.files.end());
  assert(second_iterator != state.files.end());
  assert(first_iterator->second.audio.channels() == second_iterator->second.audio.channels());
  assert(first_iterator->second.audio.frames() == second_iterator->second.audio.frames());
  for (std::size_t ch = 0U; ch < first_iterator->second.audio.channels(); ++ch) {
    const auto ch1 = first_iterator->second.audio.channel(ch);
    const auto ch2 = second_iterator->second.audio.channel(ch);
    for (std::size_t i = 0U; i < ch1.size(); ++i) {
      assert(std::abs(static_cast<double>(ch1[i]) - static_cast<double>(ch2[i])) <= tolerance);
    }
  }
}

}  // namespace amt::test

