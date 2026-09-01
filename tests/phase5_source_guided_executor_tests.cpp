#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/separation/SourceGuidedStereoExecutor.h"

namespace {

amt::separation::SourceGuidedControlPlan make_plan(
    const amt::separation::SourceGuidedAction action,
    const double amount,
    std::vector<float> activity,
    const int sample_rate,
    const std::size_t hop_frames = 1U,
    const std::optional<double> center_frequency_hz = std::nullopt,
    const std::optional<double> bandwidth_octaves = std::nullopt,
    const std::optional<double> start_seconds = std::nullopt,
    const std::optional<double> end_seconds = std::nullopt) {
  amt::separation::SourceGuidedControlPlan plan;
  plan.operates_on_canonical_stereo = true;
  plan.envelopes.push_back({.source = amt::separation::StemRole::bass,
                            .sample_rate = sample_rate,
                            .hop_frames = hop_frames,
                            .source_confidence = 1.0,
                            .activity = std::move(activity)});
  amt::separation::SourceGuidedIntervention intervention;
  intervention.source = amt::separation::StemRole::bass;
  intervention.action = action;
  intervention.amount = amount;
  intervention.confidence = 1.0;
  intervention.start_seconds = start_seconds;
  intervention.end_seconds = end_seconds;
  intervention.center_frequency_hz = center_frequency_hz;
  intervention.bandwidth_octaves = bandwidth_octaves;
  plan.bindings.push_back({.intervention = intervention, .envelope_index = 0U});
  return plan;
}

void fill(amt::audio::AudioBuffer& buffer, const float left, const float right) {
  for (std::size_t frame = 0U; frame < buffer.frames(); ++frame) {
    buffer.channel(0U)[frame] = left;
    if (buffer.channels() > 1U) buffer.channel(1U)[frame] = right;
  }
}

void test_rejects_noncanonical_plan() {
  auto plan = make_plan(amt::separation::SourceGuidedAction::gain_riding,
                        -1.0, std::vector<float>(8U, 1.0F), 48000);
  plan.operates_on_canonical_stereo = false;
  amt::separation::SourceGuidedStereoExecutor executor;
  std::string error;
  assert(!executor.initialize(plan, 48000, 2U, error));
  assert(!error.empty());
}

void test_gain_riding_hard_cap_and_zero_activity() {
  constexpr std::size_t frames = 8U;
  auto plan = make_plan(amt::separation::SourceGuidedAction::gain_riding,
                        -99.0, std::vector<float>(frames, 1.0F), 48000);
  amt::separation::SourceGuidedStereoExecutorConfig config;
  config.maximum_gain_ride_db = 99.0;

  amt::separation::SourceGuidedStereoExecutor executor;
  std::string error;
  assert(executor.initialize(plan, 48000, 2U, error, config));
  amt::audio::AudioBuffer program(2U, frames);
  fill(program, 1.0F, 1.0F);
  assert(executor.process(program, error));
  const double expected = std::pow(10.0, -1.5 / 20.0);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    assert(std::abs(static_cast<double>(program.channel(0U)[frame]) - expected) < 1.0e-5);
    assert(std::abs(static_cast<double>(program.channel(1U)[frame]) - expected) < 1.0e-5);
  }

  auto inactive_plan = make_plan(amt::separation::SourceGuidedAction::gain_riding,
                                 -1.5, std::vector<float>(frames, 0.0F), 48000);
  amt::separation::SourceGuidedStereoExecutor inactive_executor;
  assert(inactive_executor.initialize(inactive_plan, 48000, 2U, error));
  amt::audio::AudioBuffer inactive_program(2U, frames);
  fill(inactive_program, 0.37F, -0.21F);
  assert(inactive_executor.process(inactive_program, error));
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    assert(inactive_program.channel(0U)[frame] == 0.37F);
    assert(inactive_program.channel(1U)[frame] == -0.21F);
  }
}

void test_width_reduction_hard_cap() {
  constexpr std::size_t frames = 6U;
  auto plan = make_plan(amt::separation::SourceGuidedAction::stereo_width_reduction,
                        99.0, std::vector<float>(frames, 1.0F), 48000);
  amt::separation::SourceGuidedStereoExecutorConfig config;
  config.maximum_width_reduction = 99.0;
  amt::separation::SourceGuidedStereoExecutor executor;
  std::string error;
  assert(executor.initialize(plan, 48000, 2U, error, config));

  amt::audio::AudioBuffer program(2U, frames);
  fill(program, 1.0F, -1.0F);
  assert(executor.process(program, error));
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    assert(std::abs(program.channel(0U)[frame] - 0.70F) < 1.0e-6F);
    assert(std::abs(program.channel(1U)[frame] + 0.70F) < 1.0e-6F);
  }
}

void test_time_window_is_frame_accurate() {
  constexpr int sample_rate = 1000;
  constexpr std::size_t frames = 7U;
  auto plan = make_plan(amt::separation::SourceGuidedAction::gain_riding,
                        -1.5, std::vector<float>(frames + 1U, 1.0F), sample_rate,
                        1U, std::nullopt, std::nullopt, 0.002, 0.004);
  amt::separation::SourceGuidedStereoExecutor executor;
  std::string error;
  assert(executor.initialize(plan, sample_rate, 1U, error));

  amt::audio::AudioBuffer program(1U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) program.channel(0U)[frame] = 1.0F;
  assert(executor.process(program, error));
  const float reduced = static_cast<float>(std::pow(10.0, -1.5 / 20.0));
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const float expected = (frame == 2U || frame == 3U) ? reduced : 1.0F;
    assert(std::abs(program.channel(0U)[frame] - expected) < 1.0e-6F);
  }
}

void test_dynamic_eq_attenuates_target_band() {
  constexpr int sample_rate = 48000;
  constexpr std::size_t frames = 8192U;
  constexpr double frequency = 1000.0;
  auto plan = make_plan(amt::separation::SourceGuidedAction::dynamic_eq_attenuation,
                        -99.0, std::vector<float>(frames, 1.0F), sample_rate,
                        1U, frequency, 1.0);
  amt::separation::SourceGuidedStereoExecutorConfig config;
  config.maximum_dynamic_eq_cut_db = 99.0;
  amt::separation::SourceGuidedStereoExecutor executor;
  std::string error;
  assert(executor.initialize(plan, sample_rate, 1U, error, config));

  amt::audio::AudioBuffer program(1U, frames);
  std::vector<float> original(frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const float sample = static_cast<float>(0.5 * std::sin(
        2.0 * std::numbers::pi * frequency * static_cast<double>(frame) /
        static_cast<double>(sample_rate)));
    program.channel(0U)[frame] = sample;
    original[frame] = sample;
  }
  assert(executor.process(program, error));

  double input_energy = 0.0;
  double output_energy = 0.0;
  constexpr std::size_t warmup = 1024U;
  for (std::size_t frame = warmup; frame < frames; ++frame) {
    assert(std::isfinite(program.channel(0U)[frame]));
    input_energy += static_cast<double>(original[frame]) * original[frame];
    output_energy += static_cast<double>(program.channel(0U)[frame]) *
                     program.channel(0U)[frame];
  }
  const double input_rms = std::sqrt(input_energy / static_cast<double>(frames - warmup));
  const double output_rms = std::sqrt(output_energy / static_cast<double>(frames - warmup));
  assert(output_rms < input_rms * 0.90);
}

void test_transient_taming_is_bounded() {
  constexpr int sample_rate = 48000;
  constexpr std::size_t frames = 256U;
  auto plan = make_plan(amt::separation::SourceGuidedAction::transient_taming,
                        99.0, std::vector<float>(frames, 1.0F), sample_rate);
  amt::separation::SourceGuidedStereoExecutorConfig config;
  config.maximum_transient_taming = 99.0;
  amt::separation::SourceGuidedStereoExecutor executor;
  std::string error;
  assert(executor.initialize(plan, sample_rate, 2U, error, config));

  amt::audio::AudioBuffer program(2U, frames);
  program.clear();
  program.channel(0U)[0U] = 1.0F;
  program.channel(1U)[0U] = 1.0F;
  program.channel(0U)[64U] = 1.0F;
  program.channel(1U)[64U] = 1.0F;
  assert(executor.process(program, error));
  assert(std::isfinite(program.channel(0U)[0U]));
  assert(program.channel(0U)[0U] < 1.0F);
  assert(program.channel(0U)[0U] >= 0.65F);
  assert(program.channel(1U)[0U] == program.channel(0U)[0U]);
}

amt::separation::SourceGuidedControlPlan make_combined_plan(
    const int sample_rate, const std::size_t frames) {
  amt::separation::SourceGuidedControlPlan plan;
  plan.operates_on_canonical_stereo = true;
  std::vector<float> activity(frames + 2U, 0.0F);
  for (std::size_t frame = 0U; frame < activity.size(); ++frame) {
    activity[frame] = static_cast<float>(0.15 + 0.85 *
        static_cast<double>(frame) / static_cast<double>(activity.size() - 1U));
  }
  plan.envelopes.push_back({.source = amt::separation::StemRole::bass,
                            .sample_rate = sample_rate,
                            .hop_frames = 1U,
                            .source_confidence = 0.95,
                            .activity = std::move(activity)});

  const auto add = [&](const amt::separation::SourceGuidedAction action,
                       const double amount,
                       const std::optional<double> frequency = std::nullopt,
                       const std::optional<double> bandwidth = std::nullopt) {
    amt::separation::SourceGuidedIntervention intervention;
    intervention.source = amt::separation::StemRole::bass;
    intervention.action = action;
    intervention.amount = amount;
    intervention.confidence = 0.92;
    intervention.center_frequency_hz = frequency;
    intervention.bandwidth_octaves = bandwidth;
    plan.bindings.push_back({.intervention = intervention, .envelope_index = 0U});
  };
  add(amt::separation::SourceGuidedAction::gain_riding, -1.1);
  add(amt::separation::SourceGuidedAction::dynamic_eq_attenuation, -1.8, 1400.0, 0.8);
  add(amt::separation::SourceGuidedAction::transient_taming, 0.22);
  add(amt::separation::SourceGuidedAction::stereo_width_reduction, 0.18);
  return plan;
}

void test_chunk_invariance() {
  constexpr int sample_rate = 48000;
  constexpr std::size_t frames = 1024U;
  constexpr std::size_t split_at = 317U;
  const auto plan = make_combined_plan(sample_rate, frames);

  amt::audio::AudioBuffer source(2U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double time = static_cast<double>(frame) / static_cast<double>(sample_rate);
    source.channel(0U)[frame] = static_cast<float>(
        0.36 * std::sin(2.0 * std::numbers::pi * 1400.0 * time) +
        ((frame % 173U) == 0U ? 0.55 : 0.0));
    source.channel(1U)[frame] = static_cast<float>(
        0.31 * std::sin(2.0 * std::numbers::pi * 1400.0 * time + 0.4) -
        ((frame % 211U) == 0U ? 0.45 : 0.0));
  }

  auto whole = source;
  amt::separation::SourceGuidedStereoExecutor whole_executor;
  std::string error;
  assert(whole_executor.initialize(plan, sample_rate, 2U, error));
  assert(whole_executor.process(whole, error));

  amt::audio::AudioBuffer first(2U, split_at);
  amt::audio::AudioBuffer second(2U, frames - split_at);
  for (std::size_t channel = 0U; channel < 2U; ++channel) {
    for (std::size_t frame = 0U; frame < split_at; ++frame) {
      first.channel(channel)[frame] = source.channel(channel)[frame];
    }
    for (std::size_t frame = split_at; frame < frames; ++frame) {
      second.channel(channel)[frame - split_at] = source.channel(channel)[frame];
    }
  }

  amt::separation::SourceGuidedStereoExecutor chunked_executor;
  assert(chunked_executor.initialize(plan, sample_rate, 2U, error));
  assert(chunked_executor.process(first, error));
  assert(chunked_executor.process(second, error));
  assert(chunked_executor.frames_processed() == static_cast<std::int64_t>(frames));

  for (std::size_t channel = 0U; channel < 2U; ++channel) {
    for (std::size_t frame = 0U; frame < frames; ++frame) {
      const float chunked = frame < split_at
          ? first.channel(channel)[frame]
          : second.channel(channel)[frame - split_at];
      assert(std::abs(whole.channel(channel)[frame] - chunked) < 2.0e-6F);
    }
  }
}

}  // namespace

int main() {
  test_rejects_noncanonical_plan();
  test_gain_riding_hard_cap_and_zero_activity();
  test_width_reduction_hard_cap();
  test_time_window_is_frame_accurate();
  test_dynamic_eq_attenuates_target_band();
  test_transient_taming_is_bounded();
  test_chunk_invariance();
  return 0;
}
