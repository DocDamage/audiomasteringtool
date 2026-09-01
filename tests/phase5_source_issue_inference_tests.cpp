#include "phase5_test_helpers.h"

namespace {

using namespace amt::test;


amt::audio::AudioBuffer make_kick_drum_stem(const std::size_t frames,
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

amt::audio::AudioBuffer make_wide_bass_stem(const std::size_t frames,
                                             const int sample_rate = 48000) {
  amt::audio::AudioBuffer audio(2U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double time = static_cast<double>(frame) / static_cast<double>(sample_rate);
    const float sample = static_cast<float>(
        0.12 * std::sin(2.0 * std::numbers::pi * 62.0 * time));
    audio.channel(0U)[frame] = sample;
    audio.channel(1U)[frame] = -sample;
  }
  return audio;
}

amt::audio::AudioBuffer make_harsh_tonal_stem(const std::size_t frames,
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

amt::audio::AudioBuffer make_harsh_program(const std::size_t frames,
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

amt::audio::AudioBuffer make_quiet_bass_stem(const std::size_t frames,
                                              const int sample_rate = 48000) {
  amt::audio::AudioBuffer audio(1U, frames);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const double time = static_cast<double>(frame) / static_cast<double>(sample_rate);
    audio.channel(0U)[frame] = static_cast<float>(
        0.025 * std::sin(2.0 * std::numbers::pi * 55.0 * time));
  }
  return audio;
}

amt::separation::SeparationResult separation_with(
    const std::int64_t frames,
    std::initializer_list<amt::separation::SeparationArtifactReference> artifacts,
    const double confidence = 0.95) {
  amt::separation::SeparationResult separation;
  separation.sample_rate = 48000;
  separation.frames = frames;
  separation.overall_confidence = confidence;
  separation.complete_reconstruction = false;
  separation.artifacts.assign(artifacts.begin(), artifacts.end());
  return separation;
}

bool has_issue(const amt::separation::SourceIssueInferenceResult& result,
               const amt::separation::StemRole source,
               const amt::separation::SourceGuidedIssueType type) {
  return std::any_of(result.issues.begin(), result.issues.end(),
                     [&](const auto& issue) {
                       return issue.source == source && issue.type == type;
                     });
}

void test_no_stem_audio_means_no_source_attribution() {
  const auto root = test_root("issue-no-stems");
  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U * 4U;
  const auto canonical = root / "mix.wav";
  seed_audio(state, canonical, make_program(frames));
  MemoryCodec codecs(state);
  const auto canonical_analysis = analyze(codecs, canonical);

  amt::separation::SeparationResult separation;
  separation.sample_rate = 48000;
  separation.frames = static_cast<std::int64_t>(frames);
  separation.overall_confidence = 0.95;

  std::string error;
  const auto result = amt::separation::infer_source_guided_issues(
      codecs, canonical_analysis, separation, error);
  assert(result.has_value());
  assert(error.empty());
  assert(result->issues.empty());
  assert(!result->evidence.source_specific_issue);
}

void test_bass_and_drums_estimates_can_prove_low_frequency_masking() {
  const auto root = test_root("issue-masking");
  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U * 6U;
  const auto canonical = root / "mix.wav";
  const auto bass = root / "bass.wav";
  const auto drums = root / "drums.wav";
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, bass, make_bass_stem(frames));
  seed_audio(state, drums, make_kick_drum_stem(frames));
  MemoryCodec codecs(state);
  const auto canonical_analysis = analyze(codecs, canonical);

  const auto separation = separation_with(
      static_cast<std::int64_t>(frames),
      {{.kind = amt::separation::CacheArtifactKind::stem_audio,
        .role = amt::separation::StemRole::bass,
        .path = bass,
        .confidence = 0.96},
       {.kind = amt::separation::CacheArtifactKind::stem_audio,
        .role = amt::separation::StemRole::drums,
        .path = drums,
        .confidence = 0.95}});

  std::string error;
  const auto result = amt::separation::infer_source_guided_issues(
      codecs, canonical_analysis, separation, error);
  assert(result.has_value());
  assert(error.empty());
  assert(result->evidence.source_specific_issue);
  assert(has_issue(*result, amt::separation::StemRole::bass,
                   amt::separation::SourceGuidedIssueType::masking));
  const auto masking = std::find_if(result->issues.begin(), result->issues.end(),
                                    [](const auto& issue) {
                                      return issue.type ==
                                          amt::separation::SourceGuidedIssueType::masking;
                                    });
  assert(masking != result->issues.end());
  assert(masking->center_frequency_hz.has_value());
  assert(masking->evidence.find("Separated bass and drums estimates") != std::string::npos);
}

void test_bass_width_is_inferred_from_the_bass_estimate() {
  const auto root = test_root("issue-width");
  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U * 4U;
  const auto canonical = root / "mix.wav";
  const auto bass = root / "bass-wide.wav";
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, bass, make_wide_bass_stem(frames));
  MemoryCodec codecs(state);
  const auto canonical_analysis = analyze(codecs, canonical);

  const auto separation = separation_with(
      static_cast<std::int64_t>(frames),
      {{.kind = amt::separation::CacheArtifactKind::stem_audio,
        .role = amt::separation::StemRole::bass,
        .path = bass,
        .confidence = 0.98}});

  std::string error;
  const auto result = amt::separation::infer_source_guided_issues(
      codecs, canonical_analysis, separation, error);
  assert(result.has_value());
  assert(error.empty());
  assert(has_issue(*result, amt::separation::StemRole::bass,
                   amt::separation::SourceGuidedIssueType::excessive_width));
}

void test_harshness_is_inferred_from_tonal_source_energy() {
  const auto root = test_root("issue-harsh");
  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U * 4U;
  const auto canonical = root / "mix.wav";
  const auto tonal = root / "tonal.wav";
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, tonal, make_harsh_tonal_stem(frames));
  MemoryCodec codecs(state);
  const auto canonical_analysis = analyze(codecs, canonical);

  const auto separation = separation_with(
      static_cast<std::int64_t>(frames),
      {{.kind = amt::separation::CacheArtifactKind::stem_audio,
        .role = amt::separation::StemRole::tonal,
        .path = tonal,
        .confidence = 0.97}});

  std::string error;
  const auto result = amt::separation::infer_source_guided_issues(
      codecs, canonical_analysis, separation, error);
  assert(result.has_value());
  assert(error.empty());
  assert(has_issue(*result, amt::separation::StemRole::tonal,
                   amt::separation::SourceGuidedIssueType::harshness));
}

void test_generic_program_harshness_is_not_blindly_assigned_to_bass() {
  const auto root = test_root("issue-no-blind-attribution");
  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U * 4U;
  const auto canonical = root / "harsh-program.wav";
  const auto bass = root / "quiet-bass.wav";
  seed_audio(state, canonical, make_harsh_program(frames));
  seed_audio(state, bass, make_quiet_bass_stem(frames));
  MemoryCodec codecs(state);
  const auto canonical_analysis = analyze(codecs, canonical);

  const auto separation = separation_with(
      static_cast<std::int64_t>(frames),
      {{.kind = amt::separation::CacheArtifactKind::stem_audio,
        .role = amt::separation::StemRole::bass,
        .path = bass,
        .confidence = 0.97}});

  std::string error;
  const auto result = amt::separation::infer_source_guided_issues(
      codecs, canonical_analysis, separation, error);
  assert(result.has_value());
  assert(error.empty());
  assert(!has_issue(*result, amt::separation::StemRole::bass,
                    amt::separation::SourceGuidedIssueType::harshness));
}

void test_low_confidence_stem_is_not_used_for_intervention_evidence() {
  const auto root = test_root("issue-low-confidence");
  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U * 4U;
  const auto canonical = root / "mix.wav";
  const auto tonal = root / "tonal.wav";
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, tonal, make_harsh_tonal_stem(frames));
  MemoryCodec codecs(state);
  const auto canonical_analysis = analyze(codecs, canonical);

  const auto separation = separation_with(
      static_cast<std::int64_t>(frames),
      {{.kind = amt::separation::CacheArtifactKind::stem_audio,
        .role = amt::separation::StemRole::tonal,
        .path = tonal,
        .confidence = 0.40}},
      0.95);

  std::string error;
  const auto result = amt::separation::infer_source_guided_issues(
      codecs, canonical_analysis, separation, error);
  assert(result.has_value());
  assert(error.empty());
  assert(result->issues.empty());
  assert(!result->evidence.source_specific_issue);
}

}  // namespace

int main() {
  test_no_stem_audio_means_no_source_attribution();
  test_bass_and_drums_estimates_can_prove_low_frequency_masking();
  test_bass_width_is_inferred_from_the_bass_estimate();
  test_harshness_is_inferred_from_tonal_source_energy();
  test_generic_program_harshness_is_not_blindly_assigned_to_bass();
  test_low_confidence_stem_is_not_used_for_intervention_evidence();
  return 0;
}
