#include "phase5_test_helpers.h"

namespace {

using namespace amt::test;


void assert_candidate_plan_equal(
    const amt::mastering::MasteringCandidatePlan& actual,
    const amt::mastering::MasteringCandidatePlan& expected) {
  constexpr double tolerance = 1.0e-12;
  assert(actual.id == expected.id);
  assert(actual.name == expected.name);
  assert(actual.recommended == expected.recommended);
  assert(std::abs(actual.target_lufs - expected.target_lufs) <= tolerance);
  assert(std::abs(actual.ceiling_dbtp - expected.ceiling_dbtp) <= tolerance);
  assert(std::abs(actual.preservation_bias - expected.preservation_bias) <= tolerance);
  assert(actual.rationale == expected.rationale);
  assert(actual.graph.to_json() == expected.graph.to_json());
}

void assert_plan_equal(const amt::mastering::MasteringPlan& actual,
                       const amt::mastering::MasteringPlan& expected) {
  assert_candidate_plan_equal(actual.master_a, expected.master_a);
  assert_candidate_plan_equal(actual.master_b, expected.master_b);
}

void test_mode0_returns_caller_plan_as_effective_plan() {
  const auto root = test_root("effective-plan-mode0");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "mix.wav";
  write_text_file(canonical, "CANONICAL-EFFECTIVE-MODE0");

  auto state = std::make_shared<MemoryCodecState>();
  seed_audio(state, canonical, make_program(48000U));
  MemoryCodec codecs(state);
  const auto source_analysis = analyze(codecs, canonical);
  const auto caller_plan = amt::mastering::plan_mastering(source_analysis);

  amt::separation::SourceGuidanceResult guidance;
  guidance.decision.mode = amt::separation::SeparationMode::stereo_mastering;

  std::string error;
  const auto result = amt::mastering::render_mastering_plan_with_source_guidance(
      codecs, canonical, root / "out", source_analysis, caller_plan, guidance, {},
      error, {}, float_render_settings());
  assert(result.has_value());
  assert(error.empty());
  assert(!result->source_guidance_applied);
  assert(result->rendered_mode == amt::separation::SeparationMode::stereo_mastering);
  assert_plan_equal(result->effective_plan, caller_plan);
  assert(read_text_file(canonical) == "CANONICAL-EFFECTIVE-MODE0");

  std::filesystem::remove_all(root, ignored);
}

void test_mode1_returns_the_post_guidance_plan_that_rendered_masters() {
  const auto root = test_root("effective-plan-mode1");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "mix.wav";
  const auto stem = root / "bass.wav";
  write_text_file(canonical, "CANONICAL-EFFECTIVE-MODE1");
  write_text_file(stem, "STEM");

  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U;
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, stem, make_bass_stem(frames));
  MemoryCodec codecs(state);
  const auto source_analysis = analyze(codecs, canonical);

  // This plan cannot render successfully. A successful Mode 1 result therefore
  // proves the wrapper replanned after source guidance rather than reusing it.
  amt::mastering::MasteringPlan poisoned_caller_plan;
  auto guidance = guidance_for(
      stem, static_cast<std::int64_t>(frames),
      amt::separation::SeparationMode::source_guided_stereo);

  std::string error;
  const auto result = amt::mastering::render_mastering_plan_with_source_guidance(
      codecs, canonical, root / "actual", source_analysis, poisoned_caller_plan,
      guidance, bass_level_issue(), error, {}, float_render_settings());
  assert(result.has_value());
  assert(error.empty());
  assert(result->source_guidance_applied);
  assert(result->rendered_mode ==
         amt::separation::SeparationMode::source_guided_stereo);
  assert(!state->last_guided_alias.empty());

  const auto guided_analysis = analyze(codecs, state->last_guided_alias);
  const auto expected_plan = amt::mastering::plan_mastering(guided_analysis);
  assert_plan_equal(result->effective_plan, expected_plan);
  assert(!result->effective_plan.master_a.graph.empty());
  assert(!result->effective_plan.master_b.graph.empty());

  // Verify the returned effective plan is not only metadata: rendering it directly
  // from the guided program must reproduce the A/B audio returned by the wrapper.
  const auto expected = amt::mastering::render_mastering_plan(
      codecs, state->last_guided_alias, root / "expected", source_analysis,
      result->effective_plan, error, float_render_settings());
  assert(expected.has_value());
  assert(error.empty());
  assert_audio_near(*state, result->masters.master_a.output_path,
                    expected->master_a.output_path);
  assert_audio_near(*state, result->masters.master_b.output_path,
                    expected->master_b.output_path);
  assert(read_text_file(canonical) == "CANONICAL-EFFECTIVE-MODE1");

  std::filesystem::remove_all(root, ignored);
}

void test_mode1_fallback_returns_stereo_plan_as_effective_plan() {
  const auto root = test_root("effective-plan-fallback");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "mix.wav";
  const auto stem = root / "bass.wav";
  write_text_file(canonical, "CANONICAL-EFFECTIVE-FALLBACK");
  write_text_file(stem, "STEM");

  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U;
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, stem, make_bass_stem(frames));
  MemoryCodec codecs(state);
  const auto source_analysis = analyze(codecs, canonical);
  const auto caller_plan = amt::mastering::plan_mastering(source_analysis);
  auto guidance = guidance_for(
      stem, static_cast<std::int64_t>(frames),
      amt::separation::SeparationMode::source_guided_stereo);

  auto malformed = bass_level_issue();
  malformed.front().source = amt::separation::StemRole::unknown;

  std::string error;
  const auto result = amt::mastering::render_mastering_plan_with_source_guidance(
      codecs, canonical, root / "out", source_analysis, caller_plan, guidance,
      malformed, error, {}, float_render_settings());
  assert(result.has_value());
  assert(error.empty());
  assert(!result->source_guidance_applied);
  assert(result->rendered_mode == amt::separation::SeparationMode::stereo_mastering);
  assert_plan_equal(result->effective_plan, caller_plan);
  assert(read_text_file(canonical) == "CANONICAL-EFFECTIVE-FALLBACK");

  std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main() {
  test_mode0_returns_caller_plan_as_effective_plan();
  test_mode1_returns_the_post_guidance_plan_that_rendered_masters();
  test_mode1_fallback_returns_stereo_plan_as_effective_plan();
  return 0;
}
