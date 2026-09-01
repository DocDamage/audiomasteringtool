#include "amt/separation/SourceGuidedStereoExecutor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace amt::separation {
namespace {

constexpr double kHardMaximumGainRideDb = 1.5;
constexpr double kHardMaximumDynamicEqCutDb = 2.5;
constexpr double kHardMaximumWidthReduction = 0.30;
constexpr double kHardMaximumTransientTaming = 0.35;
constexpr double kMinimumBandwidthOctaves = 0.20;
constexpr double kMaximumBandwidthOctaves = 2.00;
constexpr double kEnvelopeEpsilon = 1.0e-12;

[[nodiscard]] bool finite_nonnegative(const double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] bool valid_config(const SourceGuidedStereoExecutorConfig& config) noexcept {
  return finite_nonnegative(config.maximum_gain_ride_db) &&
         finite_nonnegative(config.maximum_dynamic_eq_cut_db) &&
         finite_nonnegative(config.maximum_width_reduction) &&
         finite_nonnegative(config.maximum_transient_taming) &&
         std::isfinite(config.transient_fast_attack_ms) && config.transient_fast_attack_ms > 0.0 &&
         std::isfinite(config.transient_fast_release_ms) && config.transient_fast_release_ms > 0.0 &&
         std::isfinite(config.transient_reference_attack_ms) && config.transient_reference_attack_ms > 0.0 &&
         std::isfinite(config.transient_reference_release_ms) && config.transient_reference_release_ms > 0.0;
}

[[nodiscard]] bool valid_time_window(const SourceGuidedIntervention& intervention) noexcept {
  if (intervention.start_seconds.has_value() != intervention.end_seconds.has_value()) return false;
  if (!intervention.start_seconds) return true;
  return std::isfinite(*intervention.start_seconds) &&
         std::isfinite(*intervention.end_seconds) &&
         *intervention.start_seconds >= 0.0 &&
         *intervention.end_seconds > *intervention.start_seconds;
}

[[nodiscard]] bool valid_envelope(const SourceControlEnvelope& envelope) noexcept {
  if (envelope.source == StemRole::unknown || envelope.sample_rate <= 0 ||
      envelope.hop_frames == 0U || envelope.activity.empty() ||
      !std::isfinite(envelope.source_confidence) || envelope.source_confidence < 0.0 ||
      envelope.source_confidence > 1.0) {
    return false;
  }
  return std::all_of(envelope.activity.begin(), envelope.activity.end(), [](const float value) {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
  });
}

[[nodiscard]] int action_priority(const SourceGuidedAction action) noexcept {
  switch (action) {
    case SourceGuidedAction::gain_riding: return 0;
    case SourceGuidedAction::dynamic_eq_attenuation: return 1;
    case SourceGuidedAction::transient_taming: return 2;
    case SourceGuidedAction::stereo_width_reduction: return 3;
  }
  return 4;
}

[[nodiscard]] double db_to_linear(const double db) noexcept {
  return std::pow(10.0, db / 20.0);
}

[[nodiscard]] double smoothing_coefficient(const double milliseconds,
                                           const int sample_rate) noexcept {
  const double seconds = milliseconds * 0.001;
  return std::exp(-1.0 / (seconds * static_cast<double>(sample_rate)));
}

struct BiquadMemory {
  double x1{0.0};
  double x2{0.0};
  double y1{0.0};
  double y2{0.0};
};

struct BandpassRuntime {
  double b0{0.0};
  double b1{0.0};
  double b2{0.0};
  double a1{0.0};
  double a2{0.0};
  std::vector<BiquadMemory> channels;
};

struct TransientRuntime {
  double fast_envelope{0.0};
  double reference_envelope{0.0};
};

struct BindingRuntime {
  SourceGuidedControlBinding binding;
  BandpassRuntime bandpass;
  TransientRuntime transient;
};

[[nodiscard]] std::optional<BandpassRuntime> make_bandpass(
    const SourceGuidedIntervention& intervention,
    const int sample_rate,
    const std::size_t channels) {
  if (!intervention.center_frequency_hz ||
      !std::isfinite(*intervention.center_frequency_hz) ||
      *intervention.center_frequency_hz <= 0.0 ||
      *intervention.center_frequency_hz >= 0.48 * static_cast<double>(sample_rate)) {
    return std::nullopt;
  }

  double bandwidth = intervention.bandwidth_octaves.value_or(1.0);
  if (!std::isfinite(bandwidth) || bandwidth <= 0.0) return std::nullopt;
  bandwidth = std::clamp(bandwidth, kMinimumBandwidthOctaves, kMaximumBandwidthOctaves);

  const double ratio = std::pow(2.0, bandwidth);
  const double denominator = ratio - 1.0;
  if (!std::isfinite(ratio) || denominator <= 0.0) return std::nullopt;
  const double q = std::clamp(std::sqrt(ratio) / denominator, 0.25, 12.0);
  const double omega = 2.0 * std::numbers::pi * *intervention.center_frequency_hz /
                       static_cast<double>(sample_rate);
  const double sin_omega = std::sin(omega);
  const double alpha = sin_omega / (2.0 * q);
  const double a0 = 1.0 + alpha;
  if (!std::isfinite(a0) || std::abs(a0) <= std::numeric_limits<double>::epsilon()) {
    return std::nullopt;
  }

  BandpassRuntime runtime;
  runtime.b0 = alpha / a0;
  runtime.b1 = 0.0;
  runtime.b2 = -alpha / a0;
  runtime.a1 = (-2.0 * std::cos(omega)) / a0;
  runtime.a2 = (1.0 - alpha) / a0;
  runtime.channels.resize(channels);
  return runtime;
}

[[nodiscard]] double filter_bandpass(BandpassRuntime& runtime,
                                     const std::size_t channel,
                                     const double input) noexcept {
  auto& memory = runtime.channels[channel];
  const double output = runtime.b0 * input + runtime.b1 * memory.x1 +
                        runtime.b2 * memory.x2 - runtime.a1 * memory.y1 -
                        runtime.a2 * memory.y2;
  memory.x2 = memory.x1;
  memory.x1 = input;
  memory.y2 = memory.y1;
  memory.y1 = output;
  return output;
}

[[nodiscard]] double update_envelope(const double peak,
                                     const double previous,
                                     const double attack,
                                     const double release) noexcept {
  const double coefficient = peak > previous ? attack : release;
  return coefficient * previous + (1.0 - coefficient) * peak;
}

}  // namespace

struct SourceGuidedStereoExecutor::Impl {
  SourceGuidedControlPlan plan;
  std::vector<BindingRuntime> bindings;
  int sample_rate{0};
  std::size_t channels{0U};
  std::int64_t frames{0};
  double maximum_gain_ride_db{0.0};
  double maximum_dynamic_eq_cut_db{0.0};
  double maximum_width_reduction{0.0};
  double maximum_transient_taming{0.0};
  double fast_attack{0.0};
  double fast_release{0.0};
  double reference_attack{0.0};
  double reference_release{0.0};
  bool initialized{false};
};

SourceGuidedStereoExecutor::SourceGuidedStereoExecutor() : impl_(std::make_unique<Impl>()) {}
SourceGuidedStereoExecutor::~SourceGuidedStereoExecutor() = default;
SourceGuidedStereoExecutor::SourceGuidedStereoExecutor(SourceGuidedStereoExecutor&&) noexcept = default;
SourceGuidedStereoExecutor& SourceGuidedStereoExecutor::operator=(
    SourceGuidedStereoExecutor&&) noexcept = default;

bool SourceGuidedStereoExecutor::initialize(
    const SourceGuidedControlPlan& plan,
    const int program_sample_rate,
    const std::size_t program_channels,
    std::string& error,
    const SourceGuidedStereoExecutorConfig& config) {
  error.clear();
  impl_ = std::make_unique<Impl>();
  if (!plan.operates_on_canonical_stereo) {
    error = "source-guided stereo executor requires the canonical stereo program path";
    return false;
  }
  if (program_sample_rate <= 0 || program_channels == 0U) {
    error = "source-guided stereo executor received invalid program audio metadata";
    return false;
  }
  if (!valid_config(config)) {
    error = "source-guided stereo executor configuration is invalid";
    return false;
  }

  impl_->plan = plan;
  impl_->sample_rate = program_sample_rate;
  impl_->channels = program_channels;
  impl_->maximum_gain_ride_db =
      std::min(config.maximum_gain_ride_db, kHardMaximumGainRideDb);
  impl_->maximum_dynamic_eq_cut_db =
      std::min(config.maximum_dynamic_eq_cut_db, kHardMaximumDynamicEqCutDb);
  impl_->maximum_width_reduction =
      std::min(config.maximum_width_reduction, kHardMaximumWidthReduction);
  impl_->maximum_transient_taming =
      std::min(config.maximum_transient_taming, kHardMaximumTransientTaming);
  impl_->fast_attack = smoothing_coefficient(config.transient_fast_attack_ms,
                                              program_sample_rate);
  impl_->fast_release = smoothing_coefficient(config.transient_fast_release_ms,
                                               program_sample_rate);
  impl_->reference_attack = smoothing_coefficient(config.transient_reference_attack_ms,
                                                   program_sample_rate);
  impl_->reference_release = smoothing_coefficient(config.transient_reference_release_ms,
                                                    program_sample_rate);

  for (const auto& binding : plan.bindings) {
    if (binding.envelope_index >= plan.envelopes.size()) continue;
    const auto& envelope = plan.envelopes[binding.envelope_index];
    const auto& intervention = binding.intervention;
    if (!valid_envelope(envelope) || envelope.source != intervention.source ||
        intervention.source == StemRole::unknown || !std::isfinite(intervention.amount) ||
        !std::isfinite(intervention.confidence) || intervention.confidence < 0.0 ||
        intervention.confidence > 1.0 || !valid_time_window(intervention)) {
      continue;
    }

    BindingRuntime runtime;
    runtime.binding = binding;
    switch (intervention.action) {
      case SourceGuidedAction::gain_riding:
        if (intervention.amount >= 0.0 || impl_->maximum_gain_ride_db <= 0.0) continue;
        break;
      case SourceGuidedAction::dynamic_eq_attenuation: {
        if (intervention.amount >= 0.0 || impl_->maximum_dynamic_eq_cut_db <= 0.0) continue;
        auto bandpass = make_bandpass(intervention, program_sample_rate, program_channels);
        if (!bandpass) continue;
        runtime.bandpass = std::move(*bandpass);
        break;
      }
      case SourceGuidedAction::stereo_width_reduction:
        if (intervention.amount <= 0.0 || impl_->maximum_width_reduction <= 0.0 ||
            program_channels != 2U) {
          continue;
        }
        break;
      case SourceGuidedAction::transient_taming:
        if (intervention.amount <= 0.0 || impl_->maximum_transient_taming <= 0.0) continue;
        break;
    }
    impl_->bindings.push_back(std::move(runtime));
  }

  std::stable_sort(impl_->bindings.begin(), impl_->bindings.end(),
                   [](const BindingRuntime& first, const BindingRuntime& second) {
                     return action_priority(first.binding.intervention.action) <
                            action_priority(second.binding.intervention.action);
                   });

  if (impl_->bindings.empty()) {
    error = "no applicable source-guided stereo bindings are available";
    return false;
  }
  impl_->initialized = true;
  return true;
}

bool SourceGuidedStereoExecutor::process(amt::audio::AudioBuffer& program,
                                         std::string& error) {
  error.clear();
  if (!impl_ || !impl_->initialized) {
    error = "source-guided stereo executor is not initialized";
    return false;
  }
  if (program.channels() != impl_->channels) {
    error = "source-guided stereo executor received an unexpected channel topology";
    return false;
  }
  const auto block_frames = program.frames();
  if (block_frames == 0U) return true;
  if (block_frames > static_cast<std::size_t>(
                         std::numeric_limits<std::int64_t>::max() - impl_->frames)) {
    error = "source-guided stereo executor frame counter overflow";
    return false;
  }

  for (auto& runtime : impl_->bindings) {
    const auto action = runtime.binding.intervention.action;
    switch (action) {
      case SourceGuidedAction::gain_riding:
        for (std::size_t frame = 0U; frame < block_frames; ++frame) {
          const auto absolute_frame = impl_->frames + static_cast<std::int64_t>(frame);
          const double controlled = controlled_intervention_amount_at_frame(
              impl_->plan, runtime.binding, absolute_frame, impl_->sample_rate);
          const double gain_db = std::clamp(
              controlled, -impl_->maximum_gain_ride_db, 0.0);
          if (gain_db == 0.0) continue;
          const float gain = static_cast<float>(db_to_linear(gain_db));
          for (std::size_t channel = 0U; channel < impl_->channels; ++channel) {
            program.channel(channel)[frame] *= gain;
          }
        }
        break;

      case SourceGuidedAction::dynamic_eq_attenuation:
        for (std::size_t frame = 0U; frame < block_frames; ++frame) {
          const auto absolute_frame = impl_->frames + static_cast<std::int64_t>(frame);
          const double controlled = controlled_intervention_amount_at_frame(
              impl_->plan, runtime.binding, absolute_frame, impl_->sample_rate);
          const double cut_db = std::clamp(
              controlled, -impl_->maximum_dynamic_eq_cut_db, 0.0);
          const double band_gain = db_to_linear(cut_db) - 1.0;
          for (std::size_t channel = 0U; channel < impl_->channels; ++channel) {
            const double input = static_cast<double>(program.channel(channel)[frame]);
            const double band = filter_bandpass(runtime.bandpass, channel, input);
            program.channel(channel)[frame] =
                static_cast<float>(input + band * band_gain);
          }
        }
        break;

      case SourceGuidedAction::transient_taming:
        for (std::size_t frame = 0U; frame < block_frames; ++frame) {
          double peak = 0.0;
          for (std::size_t channel = 0U; channel < impl_->channels; ++channel) {
            peak = std::max(peak,
                            std::abs(static_cast<double>(program.channel(channel)[frame])));
          }
          runtime.transient.fast_envelope = update_envelope(
              peak, runtime.transient.fast_envelope, impl_->fast_attack, impl_->fast_release);
          runtime.transient.reference_envelope = update_envelope(
              peak, runtime.transient.reference_envelope,
              impl_->reference_attack, impl_->reference_release);
          const double denominator = std::max(runtime.transient.fast_envelope,
                                              kEnvelopeEpsilon);
          const double excess = std::clamp(
              (runtime.transient.fast_envelope - runtime.transient.reference_envelope) /
                  denominator,
              0.0, 1.0);
          const auto absolute_frame = impl_->frames + static_cast<std::int64_t>(frame);
          const double controlled = controlled_intervention_amount_at_frame(
              impl_->plan, runtime.binding, absolute_frame, impl_->sample_rate);
          const double amount = std::clamp(
              controlled, 0.0, impl_->maximum_transient_taming);
          const float gain = static_cast<float>(1.0 - amount * excess);
          for (std::size_t channel = 0U; channel < impl_->channels; ++channel) {
            program.channel(channel)[frame] *= gain;
          }
        }
        break;

      case SourceGuidedAction::stereo_width_reduction:
        for (std::size_t frame = 0U; frame < block_frames; ++frame) {
          const auto absolute_frame = impl_->frames + static_cast<std::int64_t>(frame);
          const double controlled = controlled_intervention_amount_at_frame(
              impl_->plan, runtime.binding, absolute_frame, impl_->sample_rate);
          const double reduction = std::clamp(
              controlled, 0.0, impl_->maximum_width_reduction);
          if (reduction == 0.0) continue;
          const double left = static_cast<double>(program.channel(0U)[frame]);
          const double right = static_cast<double>(program.channel(1U)[frame]);
          const double mid = 0.5 * (left + right);
          const double side = 0.5 * (left - right) * (1.0 - reduction);
          program.channel(0U)[frame] = static_cast<float>(mid + side);
          program.channel(1U)[frame] = static_cast<float>(mid - side);
        }
        break;
    }
  }

  impl_->frames += static_cast<std::int64_t>(block_frames);
  return true;
}

std::int64_t SourceGuidedStereoExecutor::frames_processed() const noexcept {
  return impl_ ? impl_->frames : 0;
}

std::size_t SourceGuidedStereoExecutor::applicable_bindings() const noexcept {
  return impl_ ? impl_->bindings.size() : 0U;
}

}  // namespace amt::separation
