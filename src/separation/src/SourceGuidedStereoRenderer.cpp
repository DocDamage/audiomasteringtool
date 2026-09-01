#include "amt/separation/SourceGuidedStereoExecutor.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>

namespace amt::separation {
namespace {

constexpr std::size_t kReadFrames = 8192U;

[[nodiscard]] std::string extension_lower(const std::filesystem::path& path) {
  const auto extension = path.extension().u8string();
  std::string value(reinterpret_cast<const char*>(extension.data()), extension.size());
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

void remove_if_exists(const std::filesystem::path& path) noexcept {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

[[nodiscard]] bool same_path(const std::filesystem::path& first,
                             const std::filesystem::path& second) noexcept {
  std::error_code first_exists_error;
  std::error_code second_exists_error;
  const bool first_exists = std::filesystem::exists(first, first_exists_error);
  const bool second_exists = std::filesystem::exists(second, second_exists_error);
  if (!first_exists_error && !second_exists_error && first_exists && second_exists) {
    std::error_code equivalent_error;
    const bool equivalent = std::filesystem::equivalent(first, second, equivalent_error);
    if (!equivalent_error) return equivalent;
  }

  std::error_code first_absolute_error;
  std::error_code second_absolute_error;
  const auto first_absolute = std::filesystem::absolute(first, first_absolute_error);
  const auto second_absolute = std::filesystem::absolute(second, second_absolute_error);
  if (first_absolute_error || second_absolute_error) return false;
  return first_absolute.lexically_normal() == second_absolute.lexically_normal();
}

[[nodiscard]] bool publish_render(const std::filesystem::path& partial,
                                  const std::filesystem::path& output,
                                  std::string& error) {
  auto backup = output;
  backup += ".bak";
  remove_if_exists(backup);

  std::error_code exists_error;
  const bool had_existing = std::filesystem::exists(output, exists_error);
  if (exists_error) {
    error = "unable to inspect source-guided stereo output path: " + exists_error.message();
    return false;
  }

  if (had_existing) {
    std::error_code stage_error;
    std::filesystem::rename(output, backup, stage_error);
    if (stage_error) {
      error = "unable to stage existing source-guided stereo output: " +
              stage_error.message();
      return false;
    }
  }

  std::error_code publish_error;
  std::filesystem::rename(partial, output, publish_error);
  if (publish_error) {
    if (had_existing) {
      std::error_code rollback_error;
      std::filesystem::rename(backup, output, rollback_error);
      if (rollback_error) {
        error = "unable to publish source-guided stereo output and rollback failed: " +
                publish_error.message() + "; rollback: " + rollback_error.message();
        return false;
      }
    }
    error = "unable to publish source-guided stereo output: " + publish_error.message();
    return false;
  }

  if (had_existing) remove_if_exists(backup);
  return true;
}

}  // namespace

std::optional<SourceGuidedStereoRenderResult> render_source_guided_stereo(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output,
    const SourceGuidedControlPlan& plan,
    std::string& error,
    const SourceGuidedStereoExecutorConfig& config,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (canonical_input.empty() || output.empty()) {
    error = "source-guided stereo render requires input and output paths";
    return std::nullopt;
  }
  if (extension_lower(output) != ".wav") {
    error = "source-guided stereo render output must be a WAV file";
    return std::nullopt;
  }
  if (same_path(canonical_input, output)) {
    error = "source-guided stereo render must not overwrite the canonical source";
    return std::nullopt;
  }
  if (!plan.operates_on_canonical_stereo) {
    error = "source-guided stereo render rejected a non-canonical processing plan";
    return std::nullopt;
  }
  if (cancellation != nullptr && cancellation->is_cancelled()) {
    error = "source-guided stereo render cancelled";
    return std::nullopt;
  }

  const auto parent = output.parent_path();
  if (!parent.empty()) {
    std::error_code directory_error;
    std::filesystem::create_directories(parent, directory_error);
    if (directory_error) {
      error = "unable to create source-guided stereo output directory: " +
              directory_error.message();
      return std::nullopt;
    }
  }

  auto partial = output;
  partial += ".partial.wav";
  auto backup = output;
  backup += ".bak";
  if (same_path(canonical_input, partial) || same_path(canonical_input, backup)) {
    error = "source-guided stereo sidecar path collides with the canonical source";
    return std::nullopt;
  }

  auto decoder = codecs.open_decoder(canonical_input, error);
  if (!decoder) return std::nullopt;
  const auto metadata = decoder->metadata();
  if (metadata.sample_rate <= 0 || metadata.channels <= 0 || metadata.frames < 0) {
    error = "canonical source metadata is invalid for source-guided stereo rendering";
    return std::nullopt;
  }

  SourceGuidedStereoExecutor executor;
  if (!executor.initialize(plan, metadata.sample_rate,
                           static_cast<std::size_t>(metadata.channels), error, config)) {
    return std::nullopt;
  }

  remove_if_exists(partial);

  amt::codec::EncodeSettings settings;
  settings.sample_rate = metadata.sample_rate;
  settings.channels = metadata.channels;
  settings.container = amt::codec::AudioContainer::wav;
  settings.sample_format = amt::codec::AudioSampleFormat::float32;
  settings.tags = metadata.tags;
  auto encoder = codecs.open_encoder(partial, settings, error);
  if (!encoder) {
    remove_if_exists(partial);
    return std::nullopt;
  }

  std::int64_t processed_frames = 0;
  while (true) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "source-guided stereo render cancelled";
      remove_if_exists(partial);
      return std::nullopt;
    }

    amt::audio::AudioBuffer block;
    std::size_t frames_read = 0U;
    if (!decoder->read(block, kReadFrames, frames_read, error, cancellation)) {
      remove_if_exists(partial);
      return std::nullopt;
    }
    if (frames_read == 0U) break;
    if (block.channels() != static_cast<std::size_t>(metadata.channels) ||
        block.frames() != frames_read) {
      error = "canonical decoder returned an unexpected source-guided stereo block";
      remove_if_exists(partial);
      return std::nullopt;
    }
    if (frames_read > static_cast<std::size_t>(
                          std::numeric_limits<std::int64_t>::max() - processed_frames)) {
      error = "source-guided stereo render frame counter overflow";
      remove_if_exists(partial);
      return std::nullopt;
    }
    if (!executor.process(block, error)) {
      remove_if_exists(partial);
      return std::nullopt;
    }
    if (!encoder->write(block, error, cancellation)) {
      remove_if_exists(partial);
      return std::nullopt;
    }
    processed_frames += static_cast<std::int64_t>(frames_read);
    if (metadata.frames > 0) {
      amt::core::report_progress(
          progress,
          std::min(0.98, static_cast<double>(processed_frames) /
                             static_cast<double>(metadata.frames)));
    }
  }

  if (cancellation != nullptr && cancellation->is_cancelled()) {
    error = "source-guided stereo render cancelled";
    remove_if_exists(partial);
    return std::nullopt;
  }
  if (!encoder->finalize(error)) {
    remove_if_exists(partial);
    return std::nullopt;
  }
  if (metadata.frames > 0 && processed_frames != metadata.frames) {
    error = "source-guided stereo render frame count does not match the canonical source";
    remove_if_exists(partial);
    return std::nullopt;
  }

  std::string probe_error;
  const auto rendered_metadata = codecs.probe(partial, probe_error);
  if (!rendered_metadata) {
    error = probe_error.empty() ? "unable to verify source-guided stereo render"
                                : probe_error;
    remove_if_exists(partial);
    return std::nullopt;
  }
  if (rendered_metadata->sample_rate != metadata.sample_rate ||
      rendered_metadata->channels != metadata.channels ||
      rendered_metadata->frames != processed_frames) {
    error = "source-guided stereo render verification failed";
    remove_if_exists(partial);
    return std::nullopt;
  }

  if (!publish_render(partial, output, error)) {
    remove_if_exists(partial);
    return std::nullopt;
  }

  amt::core::report_progress(progress, 1.0);
  return SourceGuidedStereoRenderResult{
      .output_path = output,
      .sample_rate = metadata.sample_rate,
      .channels = metadata.channels,
      .frames = processed_frames,
      .canonical_program_path = true,
      .applied_bindings = executor.applicable_bindings()};
}

}  // namespace amt::separation
