#include "amt/separation/SourceControlEnvelope.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "amt/audio/AudioBuffer.h"

namespace amt::separation {
namespace {

constexpr std::size_t kReadFrames = 8192U;
constexpr double kSilenceEpsilon = 1.0e-12;

[[nodiscard]] double clamp01(const double value) noexcept {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] bool valid_config(const SourceControlEnvelopeConfig& config) noexcept {
  return std::isfinite(config.hop_seconds) && config.hop_seconds >= 0.005 &&
         config.hop_seconds <= 0.500 &&
         std::isfinite(config.activity_floor_db) && config.activity_floor_db < 0.0 &&
         config.activity_floor_db >= -120.0 &&
         std::isfinite(config.normalization_percentile) &&
         config.normalization_percentile >= 0.50 &&
         config.normalization_percentile <= 1.0;
}

[[nodiscard]] double percentile_reference(std::vector<double> values,
                                          const double percentile) {
  values.erase(std::remove_if(values.begin(), values.end(), [](const double value) {
                 return !std::isfinite(value) || value <= kSilenceEpsilon;
               }),
               values.end());
  if (values.empty()) return 0.0;
  const double scaled = percentile * static_cast<double>(values.size() - 1U);
  const auto index = static_cast<std::size_t>(std::llround(scaled));
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index),
                   values.end());
  return values[index];
}

void normalize_activity(const std::vector<double>& rms_values,
                        const SourceControlEnvelopeConfig& config,
                        std::vector<float>& activity) {
  activity.assign(rms_values.size(), 0.0F);
  const double reference = percentile_reference(rms_values, config.normalization_percentile);
  if (reference <= kSilenceEpsilon) return;

  const double denominator = -config.activity_floor_db;
  for (std::size_t index = 0U; index < rms_values.size(); ++index) {
    const double rms = rms_values[index];
    if (!std::isfinite(rms) || rms <= kSilenceEpsilon) continue;
    const double relative_db = 20.0 * std::log10(std::max(rms / reference, kSilenceEpsilon));
    const double normalized = (relative_db - config.activity_floor_db) / denominator;
    activity[index] = static_cast<float>(clamp01(normalized));
  }
}

[[nodiscard]] bool envelope_is_usable(const SourceControlEnvelope& envelope) noexcept {
  return envelope.source != StemRole::unknown && envelope.sample_rate > 0 &&
         envelope.hop_frames > 0U && !envelope.activity.empty() &&
         std::isfinite(envelope.source_confidence) && envelope.source_confidence >= 0.0 &&
         envelope.source_confidence <= 1.0;
}

}  // namespace

std::optional<SourceControlEnvelope> build_source_control_envelope(
    amt::codec::ICodecService& codecs,
    const SeparationArtifactReference& stem_audio,
    std::string& error,
    const SourceControlEnvelopeConfig& config,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (stem_audio.kind != CacheArtifactKind::stem_audio || stem_audio.path.empty()) {
    error = "source-control envelope requires a stem-audio artifact";
    return std::nullopt;
  }
  if (stem_audio.role == StemRole::unknown) {
    error = "source-control envelope requires a known source role";
    return std::nullopt;
  }
  if (!std::isfinite(stem_audio.confidence) || stem_audio.confidence < 0.0 ||
      stem_audio.confidence > 1.0) {
    error = "source-control envelope artifact confidence is outside [0, 1]";
    return std::nullopt;
  }
  if (!valid_config(config)) {
    error = "source-control envelope configuration is invalid";
    return std::nullopt;
  }

  auto decoder = codecs.open_decoder(stem_audio.path, error);
  if (!decoder) return std::nullopt;
  const auto metadata = decoder->metadata();
  if (metadata.sample_rate <= 0 || metadata.channels <= 0) {
    error = "stem-audio metadata is invalid for source-control extraction";
    return std::nullopt;
  }

  const auto channels = static_cast<std::size_t>(metadata.channels);
  const auto hop_frames = static_cast<std::size_t>(std::max<long long>(
      1LL, std::llround(config.hop_seconds * static_cast<double>(metadata.sample_rate))));

  std::vector<double> rms_values;
  if (metadata.frames > 0) {
    const auto expected_hops = static_cast<std::uint64_t>(metadata.frames) /
                               static_cast<std::uint64_t>(hop_frames) + 1U;
    if (expected_hops < static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      rms_values.reserve(static_cast<std::size_t>(expected_hops));
    }
  }

  double hop_energy = 0.0;
  std::size_t hop_frame_count = 0U;
  std::int64_t processed_frames = 0;
  while (true) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "source-control envelope extraction cancelled";
      return std::nullopt;
    }

    amt::audio::AudioBuffer block;
    std::size_t frames_read = 0U;
    if (!decoder->read(block, kReadFrames, frames_read, error, cancellation)) {
      return std::nullopt;
    }
    if (frames_read == 0U) break;
    if (block.channels() != channels) {
      error = "stem decoder returned an unexpected channel topology";
      return std::nullopt;
    }

    for (std::size_t frame = 0U; frame < frames_read; ++frame) {
      for (std::size_t channel = 0U; channel < channels; ++channel) {
        const double sample = static_cast<double>(block.channel(channel)[frame]);
        hop_energy += sample * sample;
      }
      ++hop_frame_count;
      if (hop_frame_count == hop_frames) {
        const double samples = static_cast<double>(hop_frame_count * channels);
        rms_values.push_back(std::sqrt(hop_energy / std::max(samples, 1.0)));
        hop_energy = 0.0;
        hop_frame_count = 0U;
      }
    }

    processed_frames += static_cast<std::int64_t>(frames_read);
    if (metadata.frames > 0) {
      amt::core::report_progress(
          progress,
          std::min(1.0, static_cast<double>(processed_frames) /
                            static_cast<double>(metadata.frames)));
    }
  }

  if (hop_frame_count > 0U) {
    const double samples = static_cast<double>(hop_frame_count * channels);
    rms_values.push_back(std::sqrt(hop_energy / std::max(samples, 1.0)));
  }
  if (rms_values.empty()) {
    error = "source-control envelope received no stem-audio samples";
    return std::nullopt;
  }

  SourceControlEnvelope envelope;
  envelope.source = stem_audio.role;
  envelope.sample_rate = metadata.sample_rate;
  envelope.hop_frames = hop_frames;
  envelope.source_confidence = stem_audio.confidence;
  normalize_activity(rms_values, config, envelope.activity);
  amt::core::report_progress(progress, 1.0);
  return envelope;
}

std::optional<std::vector<SourceControlEnvelope>> build_source_control_envelopes(
    amt::codec::ICodecService& codecs,
    const SeparationResult& separation,
    std::string& error,
    const SourceControlEnvelopeConfig& config,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  std::vector<const SeparationArtifactReference*> stem_artifacts;
  for (const auto& artifact : separation.artifacts) {
    if (artifact.kind == CacheArtifactKind::stem_audio) stem_artifacts.push_back(&artifact);
  }
  if (stem_artifacts.empty()) {
    error = "no stem-audio artifacts are available for source-control extraction";
    return std::nullopt;
  }

  std::vector<SourceControlEnvelope> envelopes;
  envelopes.reserve(stem_artifacts.size());
  for (std::size_t index = 0U; index < stem_artifacts.size(); ++index) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "source-control envelope extraction cancelled";
      return std::nullopt;
    }
    const double base = static_cast<double>(index) /
                        static_cast<double>(stem_artifacts.size());
    const double span = 1.0 / static_cast<double>(stem_artifacts.size());
    auto envelope = build_source_control_envelope(
        codecs, *stem_artifacts[index], error, config, cancellation,
        [&](const double value) { amt::core::report_progress(progress, base + value * span); });
    if (!envelope) return std::nullopt;
    envelopes.push_back(std::move(*envelope));
  }
  amt::core::report_progress(progress, 1.0);
  return envelopes;
}

SourceGuidedControlPlan bind_source_guided_controls(
    const SourceGuidedProcessingPlan& processing,
    std::vector<SourceControlEnvelope> envelopes) {
  SourceGuidedControlPlan result;
  result.operates_on_canonical_stereo = true;
  result.envelopes = std::move(envelopes);
  result.skipped_reasons = processing.skipped_reasons;

  if (!processing.operates_on_canonical_stereo || processing.requires_reconstruction) {
    result.skipped_reasons.emplace_back(
        "source-guided control binding rejected a plan that does not target canonical stereo");
    return result;
  }

  for (const auto& intervention : processing.interventions) {
    std::optional<std::size_t> best_index;
    double best_confidence = -1.0;
    for (std::size_t index = 0U; index < result.envelopes.size(); ++index) {
      const auto& envelope = result.envelopes[index];
      if (!envelope_is_usable(envelope) || envelope.source != intervention.source) continue;
      if (envelope.source_confidence > best_confidence) {
        best_confidence = envelope.source_confidence;
        best_index = index;
      }
    }
    if (!best_index) {
      result.skipped_reasons.emplace_back(
          "source-guided intervention skipped because no matching source-control envelope is available");
      continue;
    }
    result.bindings.push_back({.intervention = intervention, .envelope_index = *best_index});
  }
  return result;
}

double source_activity_at_frame(const SourceControlEnvelope& envelope,
                                const std::int64_t frame) noexcept {
  if (!envelope_is_usable(envelope) || frame < 0) return 0.0;
  const auto hop = static_cast<std::uint64_t>(envelope.hop_frames);
  const auto unsigned_frame = static_cast<std::uint64_t>(frame);
  const auto index = unsigned_frame / hop;
  if (index >= envelope.activity.size()) return 0.0;
  if (index + 1U >= envelope.activity.size()) {
    return clamp01(static_cast<double>(envelope.activity[static_cast<std::size_t>(index)]));
  }

  const double fraction = static_cast<double>(unsigned_frame % hop) /
                          static_cast<double>(hop);
  const double first = clamp01(static_cast<double>(envelope.activity[static_cast<std::size_t>(index)]));
  const double second = clamp01(static_cast<double>(envelope.activity[static_cast<std::size_t>(index + 1U)]));
  return first + (second - first) * fraction;
}

double controlled_intervention_amount_at_frame(
    const SourceGuidedControlPlan& plan,
    const SourceGuidedControlBinding& binding,
    const std::int64_t frame,
    const int program_sample_rate) noexcept {
  if (!plan.operates_on_canonical_stereo || program_sample_rate <= 0 || frame < 0 ||
      binding.envelope_index >= plan.envelopes.size() ||
      !std::isfinite(binding.intervention.amount)) {
    return 0.0;
  }

  const double seconds = static_cast<double>(frame) /
                         static_cast<double>(program_sample_rate);
  if (binding.intervention.start_seconds && seconds < *binding.intervention.start_seconds) {
    return 0.0;
  }
  if (binding.intervention.end_seconds && seconds >= *binding.intervention.end_seconds) {
    return 0.0;
  }

  const auto& envelope = plan.envelopes[binding.envelope_index];
  const auto envelope_frame = static_cast<std::int64_t>(std::llround(
      seconds * static_cast<double>(envelope.sample_rate)));
  return binding.intervention.amount * source_activity_at_frame(envelope, envelope_frame);
}

}  // namespace amt::separation
