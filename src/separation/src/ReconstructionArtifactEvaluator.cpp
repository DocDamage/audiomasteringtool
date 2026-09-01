#include "amt/separation/ReconstructionArtifactEvaluator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::separation {
namespace {

constexpr std::size_t kAnalysisFrames = 8192U;
constexpr double kEnergyEpsilon = 1.0e-18;

[[nodiscard]] double clamp01(const double value) {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] double ratio_to_risk(const double ratio, const double sensitivity) {
  if (!std::isfinite(ratio) || ratio < 0.0) return 1.0;
  return clamp01(1.0 - std::exp(-ratio * sensitivity));
}

[[nodiscard]] std::string percent_text(const double value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << value * 100.0 << '%';
  return output.str();
}

[[nodiscard]] std::string correlation_text(const double value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(4) << value;
  return output.str();
}

struct ComparisonAccumulator {
  explicit ComparisonAccumulator(const std::size_t channels)
      : previous_original(channels, 0.0),
        previous_reconstructed(channels, 0.0),
        previous_original_delta(channels, 0.0),
        previous_reconstructed_delta(channels, 0.0) {}

  void add(const std::size_t channel, const double original,
           const double reconstructed) {
    const double residual = reconstructed - original;
    original_energy += original * original;
    reconstructed_energy += reconstructed * reconstructed;
    cross_energy += original * reconstructed;
    residual_energy += residual * residual;

    if (have_previous) {
      const double original_delta = original - previous_original[channel];
      const double reconstructed_delta = reconstructed - previous_reconstructed[channel];
      const double delta_error = reconstructed_delta - original_delta;
      transient_reference_energy += original_delta * original_delta;
      transient_error_energy += delta_error * delta_error;

      if (have_previous_delta) {
        const double original_second = original_delta - previous_original_delta[channel];
        const double reconstructed_second =
            reconstructed_delta - previous_reconstructed_delta[channel];
        const double second_error = reconstructed_second - original_second;
        high_frequency_reference_energy += original_second * original_second;
        high_frequency_error_energy += second_error * second_error;
      }
      previous_original_delta[channel] = original_delta;
      previous_reconstructed_delta[channel] = reconstructed_delta;
    }

    previous_original[channel] = original;
    previous_reconstructed[channel] = reconstructed;
    ++sample_count;
  }

  void finish_frame() {
    if (have_previous) have_previous_delta = true;
    have_previous = true;
  }

  std::uint64_t sample_count{0U};
  double original_energy{0.0};
  double reconstructed_energy{0.0};
  double cross_energy{0.0};
  double residual_energy{0.0};
  double transient_reference_energy{0.0};
  double transient_error_energy{0.0};
  double high_frequency_reference_energy{0.0};
  double high_frequency_error_energy{0.0};
  std::vector<double> previous_original;
  std::vector<double> previous_reconstructed;
  std::vector<double> previous_original_delta;
  std::vector<double> previous_reconstructed_delta;
  bool have_previous{false};
  bool have_previous_delta{false};
};

[[nodiscard]] ReconstructionComparisonMetrics finalize_metrics(
    const ComparisonAccumulator& accumulator,
    const int sample_rate,
    const std::size_t channels) {
  ReconstructionComparisonMetrics metrics;
  metrics.sample_count = accumulator.sample_count;
  if (accumulator.sample_count == 0U || channels == 0U || sample_rate <= 0) return metrics;

  metrics.residual_ratio = std::sqrt(
      accumulator.residual_energy /
      std::max(accumulator.original_energy, kEnergyEpsilon));
  metrics.transient_mismatch_ratio = std::sqrt(
      accumulator.transient_error_energy /
      std::max(accumulator.transient_reference_energy, kEnergyEpsilon));
  metrics.high_frequency_mismatch_ratio = std::sqrt(
      accumulator.high_frequency_error_energy /
      std::max(accumulator.high_frequency_reference_energy, kEnergyEpsilon));

  const double denominator = std::sqrt(
      std::max(accumulator.original_energy, kEnergyEpsilon) *
      std::max(accumulator.reconstructed_energy, kEnergyEpsilon));
  metrics.correlation = denominator > kEnergyEpsilon
      ? std::clamp(accumulator.cross_energy / denominator, -1.0, 1.0)
      : 0.0;

  const double frames = static_cast<double>(accumulator.sample_count) /
                        static_cast<double>(channels);
  const double seconds = frames / static_cast<double>(sample_rate);
  const double duration_factor = clamp01(seconds / 30.0);
  metrics.measurement_confidence = 0.55 + duration_factor * 0.35;
  return metrics;
}

}  // namespace

ArtifactAssessment assess_reconstruction_comparison(
    const ReconstructionComparisonMetrics& metrics,
    const double model_confidence) {
  const double residual_risk = ratio_to_risk(metrics.residual_ratio, 4.0);
  const double phase_risk = clamp01((1.0 - std::clamp(metrics.correlation, -1.0, 1.0)) * 0.5);
  const double transient_risk = ratio_to_risk(metrics.transient_mismatch_ratio, 2.5);
  const double high_frequency_risk = ratio_to_risk(metrics.high_frequency_mismatch_ratio, 2.0);
  const double measured_risk = 0.46 * residual_risk +
                               0.20 * phase_risk +
                               0.22 * transient_risk +
                               0.12 * high_frequency_risk;

  const double measured_confidence = clamp01(metrics.measurement_confidence);
  const double model = clamp01(model_confidence);
  const double uncertainty_penalty = (1.0 - measured_confidence) * 0.10 +
                                     (1.0 - model) * 0.10 + 0.03;

  ArtifactAssessment assessment;
  assessment.overall_risk = clamp01(
      std::max(measured_risk, residual_risk * 0.72) + uncertainty_penalty);
  // This evaluator can measure reconstruction agreement, but it does not have
  // isolated ground truth for per-stem leakage or musical-noise labels.
  assessment.confidence = clamp01(std::sqrt(model * measured_confidence) * 0.90);
  assessment.evidence.emplace_back(
      "stem-sum reconstruction residual proxy: " + percent_text(metrics.residual_ratio));
  assessment.evidence.emplace_back(
      "original/reconstruction correlation: " + correlation_text(metrics.correlation));
  assessment.evidence.emplace_back(
      "transient mismatch proxy: " + percent_text(metrics.transient_mismatch_ratio));
  assessment.evidence.emplace_back(
      "high-frequency mismatch proxy: " + percent_text(metrics.high_frequency_mismatch_ratio));
  assessment.evidence.emplace_back(
      "per-stem leakage and musical-noise ground truth are unavailable; they are not claimed as directly measured");
  if (metrics.sample_count == 0U) {
    assessment.evidence.emplace_back("no comparable samples were available");
  }
  return assessment;
}

std::optional<ArtifactAssessment> StreamingReconstructionArtifactEvaluator::evaluate(
    const std::filesystem::path& original_source,
    const SeparationResult& separation,
    std::string& error,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  if (original_source.empty()) {
    error = "reconstruction evaluation is missing the original source";
    return std::nullopt;
  }
  if (!separation.complete_reconstruction) {
    error = "reconstruction evaluation requires a complete reconstructable stem set";
    return std::nullopt;
  }

  std::vector<std::filesystem::path> stem_paths;
  for (const auto& artifact : separation.artifacts) {
    if (artifact.kind == CacheArtifactKind::stem_audio) stem_paths.push_back(artifact.path);
  }
  if (stem_paths.empty()) {
    error = "reconstruction evaluation requires stem-audio artifacts";
    return std::nullopt;
  }

  auto original_decoder = codecs_.open_decoder(original_source, error);
  if (!original_decoder) return std::nullopt;
  const auto original_metadata = original_decoder->metadata();
  if (original_metadata.sample_rate <= 0 || original_metadata.channels <= 0) {
    error = "original source metadata is invalid for reconstruction evaluation";
    return std::nullopt;
  }

  std::vector<std::unique_ptr<amt::codec::IAudioDecoder>> stem_decoders;
  stem_decoders.reserve(stem_paths.size());
  for (const auto& stem_path : stem_paths) {
    std::string stem_error;
    auto decoder = codecs_.open_decoder(stem_path, stem_error);
    if (!decoder) {
      error = "unable to open reconstruction stem: " + stem_error;
      return std::nullopt;
    }
    const auto metadata = decoder->metadata();
    if (metadata.sample_rate != original_metadata.sample_rate ||
        metadata.channels != original_metadata.channels) {
      error = "reconstruction stem sample rate/channel topology does not match the original";
      return std::nullopt;
    }
    if (original_metadata.frames > 0 && metadata.frames > 0 &&
        metadata.frames != original_metadata.frames) {
      error = "reconstruction stem duration does not match the original";
      return std::nullopt;
    }
    stem_decoders.push_back(std::move(decoder));
  }

  const auto channels = static_cast<std::size_t>(original_metadata.channels);
  ComparisonAccumulator accumulator(channels);
  std::int64_t processed_frames = 0;

  while (true) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "reconstruction artifact evaluation cancelled";
      return std::nullopt;
    }

    amt::audio::AudioBuffer original_block;
    std::size_t original_frames = 0U;
    if (!original_decoder->read(original_block, kAnalysisFrames, original_frames,
                                error, cancellation)) {
      return std::nullopt;
    }

    std::vector<amt::audio::AudioBuffer> stem_blocks(stem_decoders.size());
    for (std::size_t stem_index = 0U; stem_index < stem_decoders.size(); ++stem_index) {
      std::size_t stem_frames = 0U;
      if (!stem_decoders[stem_index]->read(stem_blocks[stem_index], kAnalysisFrames,
                                          stem_frames, error, cancellation)) {
        return std::nullopt;
      }
      if (stem_frames != original_frames) {
        error = "reconstruction stem stream length diverged from the original";
        return std::nullopt;
      }
    }

    if (original_frames == 0U) break;
    if (original_block.channels() != channels || original_block.frames() != original_frames) {
      error = "original decoder returned an unexpected audio block topology";
      return std::nullopt;
    }
    for (const auto& stem_block : stem_blocks) {
      if (stem_block.channels() != channels || stem_block.frames() != original_frames) {
        error = "stem decoder returned an unexpected audio block topology";
        return std::nullopt;
      }
    }

    for (std::size_t frame = 0U; frame < original_frames; ++frame) {
      for (std::size_t channel = 0U; channel < channels; ++channel) {
        double reconstructed = 0.0;
        for (const auto& stem_block : stem_blocks) {
          reconstructed += static_cast<double>(stem_block.channel(channel)[frame]);
        }
        accumulator.add(channel,
                        static_cast<double>(original_block.channel(channel)[frame]),
                        reconstructed);
      }
      accumulator.finish_frame();
    }

    processed_frames += static_cast<std::int64_t>(original_frames);
    if (original_metadata.frames > 0) {
      amt::core::report_progress(
          progress,
          std::min(1.0, static_cast<double>(processed_frames) /
                            static_cast<double>(original_metadata.frames)));
    }
  }

  if (accumulator.sample_count == 0U) {
    error = "reconstruction artifact evaluation received no audio samples";
    return std::nullopt;
  }

  amt::core::report_progress(progress, 1.0);
  const auto metrics = finalize_metrics(accumulator, original_metadata.sample_rate, channels);
  return assess_reconstruction_comparison(metrics, separation.overall_confidence);
}

}  // namespace amt::separation
