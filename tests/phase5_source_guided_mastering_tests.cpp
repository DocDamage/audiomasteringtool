#include "phase5_test_helpers.h"

namespace {

using namespace amt::test;


bool warning_contains(const std::vector<std::string>& warnings,
                      const std::string& needle) {
  return std::any_of(warnings.begin(), warnings.end(), [&](const std::string& warning) {
    return warning.find(needle) != std::string::npos;
  });
}

bool has_work_directory(const std::filesystem::path& output_directory) {
  std::error_code error;
  if (!std::filesystem::is_directory(output_directory, error) || error) return false;
  for (const auto& entry : std::filesystem::directory_iterator(output_directory)) {
    const auto name = entry.path().filename().string();
    if (name.rfind(".amt-source-guided-work-", 0U) == 0U) return true;
  }
  return false;
}


void assert_profile_near(const amt::mastering::LoudnessMatchProfile& actual,
                         const amt::mastering::LoudnessMatchProfile& expected) {
  constexpr double tolerance = 1.0e-9;
  assert(std::abs(actual.reference_lufs - expected.reference_lufs) <= tolerance);
  assert(std::abs(actual.original_gain_db - expected.original_gain_db) <= tolerance);
  assert(std::abs(actual.master_a_gain_db - expected.master_a_gain_db) <= tolerance);
  assert(std::abs(actual.master_b_gain_db - expected.master_b_gain_db) <= tolerance);
}

void test_mode0_matches_existing_renderer() {
  const auto root = test_root("mode0");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "mix.wav";
  write_text_file(canonical, "CANONICAL-MODE0");

  auto state = std::make_shared<MemoryCodecState>();
  seed_audio(state, canonical, make_program(48000U));
  MemoryCodec codecs(state);
  const auto source_analysis = analyze(codecs, canonical);
  const auto plan = amt::mastering::plan_mastering(source_analysis);
  amt::separation::SourceGuidanceResult guidance;
  guidance.decision.mode = amt::separation::SeparationMode::stereo_mastering;

  std::string error;
  const auto actual = amt::mastering::render_mastering_plan_with_source_guidance(
      codecs, canonical, root / "wrapper", source_analysis, plan, guidance, {},
      error, {}, float_render_settings());
  assert(actual.has_value());
  assert(error.empty());
  assert(!actual->source_guidance_applied);
  assert(actual->rendered_mode == amt::separation::SeparationMode::stereo_mastering);
  assert(actual->applied_bindings == 0U);
  assert(state->last_guided_alias.empty());
  assert(!has_work_directory(root / "wrapper"));

  const auto expected = amt::mastering::render_mastering_plan(
      codecs, canonical, root / "direct", source_analysis, plan, error,
      float_render_settings());
  assert(expected.has_value());
  assert(error.empty());
  assert_audio_near(*state, actual->masters.master_a.output_path,
                    expected->master_a.output_path);
  assert_audio_near(*state, actual->masters.master_b.output_path,
                    expected->master_b.output_path);
  assert_profile_near(actual->masters.audition, expected->audition);
  assert(read_text_file(canonical) == "CANONICAL-MODE0");

  std::filesystem::remove_all(root, ignored);
}

void test_mode1_reanalyzes_and_replans_guided_stereo() {
  const auto root = test_root("mode1-replan");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "mix.wav";
  const auto stem = root / "bass.wav";
  write_text_file(canonical, "CANONICAL-MODE1");
  write_text_file(stem, "STEM");

  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U;
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, stem, make_bass_stem(frames));
  MemoryCodec codecs(state);
  const auto source_analysis = analyze(codecs, canonical);

  // Deliberately invalid. Old Mode 1 behavior reused this pre-guidance plan and
  // therefore cannot render. Correct Mode 1 must ignore it after guidance,
  // re-analyze the guided stereo render, and build a fresh plan from that report.
  amt::mastering::MasteringPlan poisoned_original_plan;
  auto guidance = guidance_for(
      stem, static_cast<std::int64_t>(frames),
      amt::separation::SeparationMode::source_guided_stereo);

  std::string error;
  const auto result = amt::mastering::render_mastering_plan_with_source_guidance(
      codecs, canonical, root / "actual", source_analysis, poisoned_original_plan,
      guidance, bass_level_issue(), error, {}, float_render_settings());
  assert(result.has_value());
  assert(error.empty());
  assert(result->source_guidance_applied);
  assert(result->rendered_mode ==
         amt::separation::SeparationMode::source_guided_stereo);
  assert(result->applied_bindings == 1U);
  assert(read_text_file(canonical) == "CANONICAL-MODE1");
  assert(!state->last_guided_alias.empty());
  assert(!has_work_directory(root / "actual"));
  assert(!std::filesystem::exists(state->last_guided_alias.parent_path()));

  // Independently derive the expected downstream plan from the guided render
  // captured by the memory codec and verify both actual masters match a direct
  // render using that post-guidance analysis/plan.
  const auto guided_analysis = analyze(codecs, state->last_guided_alias);
  const auto expected_plan = amt::mastering::plan_mastering(guided_analysis);
  const auto expected = amt::mastering::render_mastering_plan(
      codecs, state->last_guided_alias, root / "expected", source_analysis,
      expected_plan, error, float_render_settings());
  assert(expected.has_value());
  assert(error.empty());
  assert_audio_near(*state, result->masters.master_a.output_path,
                    expected->master_a.output_path);
  assert_audio_near(*state, result->masters.master_b.output_path,
                    expected->master_b.output_path);

  const auto expected_audition = amt::mastering::make_loudness_match_profile(
      source_analysis.loudness, result->masters.master_a.analysis.loudness,
      result->masters.master_b.analysis.loudness);
  assert_profile_near(result->masters.audition, expected_audition);

  std::filesystem::remove_all(root, ignored);
}

void test_malformed_mode1_falls_back_to_stereo() {
  const auto root = test_root("fallback");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "mix.wav";
  const auto stem = root / "bass.wav";
  write_text_file(canonical, "CANONICAL-FALLBACK");
  write_text_file(stem, "STEM");

  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U;
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, stem, make_bass_stem(frames));
  MemoryCodec codecs(state);
  const auto source_analysis = analyze(codecs, canonical);
  const auto plan = amt::mastering::plan_mastering(source_analysis);
  auto guidance = guidance_for(
      stem, static_cast<std::int64_t>(frames),
      amt::separation::SeparationMode::source_guided_stereo);

  auto malformed = bass_level_issue();
  malformed.front().source = amt::separation::StemRole::unknown;

  std::string error;
  const auto result = amt::mastering::render_mastering_plan_with_source_guidance(
      codecs, canonical, root / "out", source_analysis, plan, guidance, malformed,
      error, {}, float_render_settings());
  assert(result.has_value());
  assert(error.empty());
  assert(!result->source_guidance_applied);
  assert(result->requested_mode ==
         amt::separation::SeparationMode::source_guided_stereo);
  assert(result->rendered_mode == amt::separation::SeparationMode::stereo_mastering);
  assert(warning_contains(result->warnings, "fell back"));
  assert(read_text_file(canonical) == "CANONICAL-FALLBACK");
  assert(!has_work_directory(root / "out"));

  std::filesystem::remove_all(root, ignored);
}

void test_mode2_never_silently_reconstructs() {
  const auto root = test_root("mode2");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "mix.wav";
  const auto stem = root / "bass.wav";
  write_text_file(canonical, "CANONICAL-MODE2");
  write_text_file(stem, "STEM");

  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U;
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, stem, make_bass_stem(frames));
  MemoryCodec codecs(state);
  const auto source_analysis = analyze(codecs, canonical);
  amt::mastering::MasteringPlan poisoned_original_plan;
  auto guidance = guidance_for(
      stem, static_cast<std::int64_t>(frames),
      amt::separation::SeparationMode::stem_reconstruction);
  assert(guidance.separation.has_value());
  assert(!guidance.separation->complete_reconstruction);

  std::string error;
  const auto result = amt::mastering::render_mastering_plan_with_source_guidance(
      codecs, canonical, root / "out", source_analysis, poisoned_original_plan,
      guidance, bass_level_issue(), error, {}, float_render_settings());
  assert(result.has_value());
  assert(error.empty());
  assert(result->requested_mode ==
         amt::separation::SeparationMode::stem_reconstruction);
  assert(result->rendered_mode ==
         amt::separation::SeparationMode::source_guided_stereo);
  assert(result->source_guidance_applied);
  assert(warning_contains(result->warnings, "Mode 2 was requested"));
  assert(read_text_file(canonical) == "CANONICAL-MODE2");
  assert(!has_work_directory(root / "out"));

  std::filesystem::remove_all(root, ignored);
}

void test_cancellation_cleans_mode1_work_files() {
  const auto root = test_root("cancel");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  const auto canonical = root / "mix.wav";
  const auto stem = root / "bass.wav";
  write_text_file(canonical, "CANONICAL-CANCEL");
  write_text_file(stem, "STEM");

  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U;
  seed_audio(state, canonical, make_program(frames));
  seed_audio(state, stem, make_bass_stem(frames));
  MemoryCodec codecs(state);
  const auto source_analysis = analyze(codecs, canonical);
  const auto plan = amt::mastering::plan_mastering(source_analysis);
  auto guidance = guidance_for(
      stem, static_cast<std::int64_t>(frames),
      amt::separation::SeparationMode::source_guided_stereo);

  amt::core::CancellationToken cancellation;
  std::string error;
  const auto result = amt::mastering::render_mastering_plan_with_source_guidance(
      codecs, canonical, root / "out", source_analysis, plan, guidance,
      bass_level_issue(), error, {}, float_render_settings(), &cancellation,
      [&cancellation](const double progress) {
        if (progress > 0.21) cancellation.cancel();
      });
  assert(!result.has_value());
  assert(cancellation.is_cancelled());
  assert(!error.empty());
  assert(read_text_file(canonical) == "CANONICAL-CANCEL");
  assert(!has_work_directory(root / "out"));

  std::filesystem::remove_all(root, ignored);
}

}  // namespace

int main() {
  test_mode0_matches_existing_renderer();
  test_mode1_reanalyzes_and_replans_guided_stereo();
  test_malformed_mode1_falls_back_to_stereo();
  test_mode2_never_silently_reconstructs();
  test_cancellation_cleans_mode1_work_files();
  return 0;
}
