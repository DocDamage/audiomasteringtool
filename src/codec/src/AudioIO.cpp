#include "amt/codec/AudioIO.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <random>
#include <utility>

#include "amt/audio/Resampler.h"

namespace amt::codec {
namespace {

constexpr std::size_t kStreamingFrames = 8192U;

bool is_integer_format(const AudioSampleFormat format) {
  return integer_bit_depth(format) > 0;
}

bool is_floating_format(const AudioSampleFormat format) noexcept {
  return format == AudioSampleFormat::float32 || format == AudioSampleFormat::float64;
}

class TpdfDither {
 public:
  explicit TpdfDither(const int target_bits)
      : lsb_(target_bits > 0 ? std::ldexp(1.0, -(target_bits - 1)) : 0.0) {}

  void apply(amt::audio::AudioBuffer& buffer) {
    if (lsb_ == 0.0) return;
    for (std::size_t channel_index = 0; channel_index < buffer.channels(); ++channel_index) {
      auto channel = buffer.channel(channel_index);
      for (float& sample : channel) {
        const double noise = (distribution_(generator_) + distribution_(generator_)) * lsb_;
        sample = static_cast<float>(static_cast<double>(sample) + noise);
      }
    }
  }

 private:
  double lsb_{0.0};
  std::minstd_rand generator_{0x41554D54U};
  std::uniform_real_distribution<double> distribution_{-0.5, 0.5};
};

}  // namespace

AudioContainer container_from_extension(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
  if (extension == ".wav" || extension == ".wave") return AudioContainer::wav;
  if (extension == ".aif" || extension == ".aiff") return AudioContainer::aiff;
  if (extension == ".flac") return AudioContainer::flac;
  if (extension == ".ogg") return AudioContainer::ogg;
  if (extension == ".mp3") return AudioContainer::mp3;
  if (extension == ".m4a" || extension == ".aac") return AudioContainer::aac_m4a;
  if (extension == ".opus") return AudioContainer::opus;
  return AudioContainer::unknown;
}

ChannelLayout channel_layout_from_count(const int channels) noexcept {
  switch (channels) {
    case 1:
      return ChannelLayout::mono;
    case 2:
      return ChannelLayout::stereo;
    case 3:
      return ChannelLayout::three_zero;
    case 4:
      return ChannelLayout::quad;
    case 5:
      return ChannelLayout::five_zero;
    case 6:
      return ChannelLayout::five_one;
    default:
      return ChannelLayout::unknown;
  }
}

int integer_bit_depth(const AudioSampleFormat format) noexcept {
  switch (format) {
    case AudioSampleFormat::pcm16:
      return 16;
    case AudioSampleFormat::pcm24:
      return 24;
    case AudioSampleFormat::pcm32:
      return 32;
    default:
      return 0;
  }
}

bool export_audio(ICodecService& codecs, const std::filesystem::path& input,
                  const std::filesystem::path& output, const ExportRequest& request,
                  std::string& error, const amt::core::CancellationToken* cancellation,
                  const amt::core::ProgressCallback& progress) {
  auto decoder = codecs.open_decoder(input, error);
  if (!decoder) return false;
  const auto source = decoder->metadata();

  EncodeSettings settings;
  settings.sample_rate = request.sample_rate.value_or(source.sample_rate);
  settings.channels = source.channels;
  settings.container = request.container.value_or(container_from_extension(output));
  if (settings.container == AudioContainer::unknown) settings.container = source.container;
  settings.sample_format = request.sample_format.value_or(source.sample_format);
  settings.tags = source.tags;

  auto encoder = codecs.open_encoder(output, settings, error);
  if (!encoder) return false;

  std::unique_ptr<amt::audio::IStreamingResampler> resampler;
  if (settings.sample_rate != source.sample_rate) {
    resampler = amt::audio::make_high_quality_resampler(
        {.input_sample_rate = source.sample_rate,
         .output_sample_rate = settings.sample_rate,
         .channels = static_cast<std::size_t>(source.channels)});
  }

  const int source_bits = integer_bit_depth(source.sample_format);
  const int target_bits = integer_bit_depth(settings.sample_format);
  const bool quantizes_from_float = is_floating_format(source.sample_format) && target_bits > 0;
  const bool reduces_integer_depth = source_bits > 0 && target_bits > 0 && source_bits > target_bits;
  const bool should_dither = request.dither_when_reducing_integer_depth &&
                             is_integer_format(settings.sample_format) &&
                             (quantizes_from_float || reduces_integer_depth);
  TpdfDither dither(should_dither ? target_bits : 0);

  std::int64_t consumed = 0;
  while (true) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "export cancelled";
      return false;
    }
    amt::audio::AudioBuffer decoded;
    std::size_t frames_read = 0;
    if (!decoder->read(decoded, kStreamingFrames, frames_read, error, cancellation)) return false;
    const bool done = frames_read == 0U;
    amt::audio::AudioBuffer rendered;
    if (resampler) {
      rendered = resampler->process(decoded, done);
    } else {
      rendered = std::move(decoded);
    }
    if (!rendered.empty()) {
      dither.apply(rendered);
      if (!encoder->write(rendered, error, cancellation)) return false;
    }
    if (done) break;
    consumed += static_cast<std::int64_t>(frames_read);
    if (source.frames > 0) {
      amt::core::report_progress(progress, static_cast<double>(consumed) /
                                               static_cast<double>(source.frames));
    }
  }

  if (!encoder->finalize(error)) return false;
  amt::core::report_progress(progress, 1.0);
  return true;
}

bool verify_audio_equal(ICodecService& codecs, const std::filesystem::path& first,
                        const std::filesystem::path& second, const double tolerance,
                        std::string& error, const amt::core::CancellationToken* cancellation) {
  if (tolerance < 0.0) {
    error = "comparison tolerance cannot be negative";
    return false;
  }
  auto a = codecs.open_decoder(first, error);
  if (!a) return false;
  auto b = codecs.open_decoder(second, error);
  if (!b) return false;
  const auto& a_info = a->metadata();
  const auto& b_info = b->metadata();
  if (a_info.sample_rate != b_info.sample_rate || a_info.channels != b_info.channels ||
      a_info.frames != b_info.frames) {
    error = "decoded audio geometry mismatch";
    return false;
  }

  std::int64_t compared = 0;
  while (true) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "audio comparison cancelled";
      return false;
    }
    amt::audio::AudioBuffer a_buffer;
    amt::audio::AudioBuffer b_buffer;
    std::size_t a_frames = 0U;
    std::size_t b_frames = 0U;
    if (!a->read(a_buffer, kStreamingFrames, a_frames, error, cancellation) ||
        !b->read(b_buffer, kStreamingFrames, b_frames, error, cancellation)) {
      return false;
    }
    if (a_frames != b_frames) {
      error = "decoded frame-count mismatch near frame " + std::to_string(compared);
      return false;
    }
    if (a_frames == 0U) break;
    for (std::size_t channel = 0; channel < a_buffer.channels(); ++channel) {
      const auto a_channel = a_buffer.channel(channel);
      const auto b_channel = b_buffer.channel(channel);
      for (std::size_t frame = 0; frame < a_frames; ++frame) {
        const float av = a_channel[frame];
        const float bv = b_channel[frame];
        if (!std::isfinite(av) || !std::isfinite(bv) ||
            std::abs(static_cast<double>(av) - static_cast<double>(bv)) > tolerance) {
          error = "decoded sample mismatch near frame " +
                  std::to_string(compared + static_cast<std::int64_t>(frame));
          return false;
        }
      }
    }
    compared += static_cast<std::int64_t>(a_frames);
  }
  return true;
}

}  // namespace amt::codec
