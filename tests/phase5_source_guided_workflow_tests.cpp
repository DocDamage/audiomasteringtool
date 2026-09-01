#define main phase5_source_issue_fixture_main
#include "phase5_source_issue_inference_tests.cpp"
#undef main

#include "amt/separation/SourceGuidedWorkflow.h"

namespace {

class MemoryWorkflowProvider final : public amt::separation::ISeparationProvider {
 public:
  MemoryWorkflowProvider(std::shared_ptr<MemoryCodecState> state,
                         amt::audio::AudioBuffer stem_audio,
                         amt::separation::StemRole role)
      : state_(std::move(state)), stem_audio_(std::move(stem_audio)), role_(role) {
    model_.model_name = "workflow-test-separator";
    model_.model_version = "1.0.0";
    model_.model_sha256 = std::string(64U, 'b');
    model_.architecture_source = "synthetic test architecture";
    model_.weight_provenance = "synthetic test weights";
    model_.code_license = "MIT";
    model_.weights_license = "Internal test license";
    model_.redistribution_reviewed = true;
    model_.redistribution_allowed = true;
    model_.commercial_use_reviewed = true;
    model_.commercial_use_allowed = true;
    model_.supported_execution_providers = {"CPU"};
    model_.expected_input_sample_rate = 48000;
    model_.stem_taxonomy = {role_};
    model_.benchmark_record = "synthetic-workflow-test";
    model_.security_reviewed = true;
  }

  bool available() const noexcept override { return available_; }

  amt::separation::SeparationModelManifest model_manifest() const override {
    return model_;
  }

  std::optional<amt::separation::SeparationResult> separate(
      const amt::separation::SeparationRequest& request,
      std::string& error,
      const amt::core::CancellationToken* cancellation,
      const amt::core::ProgressCallback& progress) override {
    ++calls_;
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "workflow provider cancelled";
      return std::nullopt;
    }
    if (!available_) {
      error = "workflow provider unavailable";
      return std::nullopt;
    }

    const auto output_root = request.cache_directory.empty()
        ? request.source_path.parent_path() / "workflow-estimates"
        : request.cache_directory;
    const auto stem_path = output_root / (amt::separation::stem_role_name(role_) + ".wav");
    seed_audio(state_, stem_path, stem_audio_);

    amt::separation::SeparationResult result;
    result.model = model_;
    result.sample_rate = 48000;
    result.frames = static_cast<std::int64_t>(stem_audio_.frames());
    result.overall_confidence = confidence_;
    result.complete_reconstruction = false;
    result.artifacts.push_back({
        .kind = amt::separation::CacheArtifactKind::stem_audio,
        .role = role_,
        .path = stem_path,
        .confidence = confidence_});
    amt::core::report_progress(progress, 1.0);
    return result;
  }

  std::shared_ptr<MemoryCodecState> state_;
  amt::audio::AudioBuffer stem_audio_;
  amt::separation::StemRole role_{amt::separation::StemRole::unknown};
  amt::separation::SeparationModelManifest model_;
  bool available_{true};
  double confidence_{0.96};
  int calls_{0};
};

amt::separation::SourceGuidanceRequest workflow_request(
    const std::filesystem::path& canonical) {
  amt::separation::SourceGuidanceRequest request;
  request.separation.source_path = canonical;
  request.separation.request_stem_audio = true;
  request.separation.request_time_frequency_masks = false;
  request.separation.requested_stems = {
      amt::separation::StemRole::vocals,
      amt::separation::StemRole::drums,
      amt::separation::StemRole::bass,
      amt::separation::StemRole::other,
      amt::separation::StemRole::tonal};
  return request;
}

amt::separation::SourceGuidedWorkflowConfig workflow_config() {
  amt::separation::SourceGuidedWorkflowConfig config;
  config.guidance.require_bundled_production_model_eligibility = false;
  config.guidance.enable_cache = false;
  config.guidance.compute_missing_source_fingerprint = false;
  return config;
}

void test_diagnostic_separation_requires_post_separation_evidence_for_mode1() {
  const auto root = test_root("workflow-mode1");
  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U * 4U;
  const auto canonical = root / "mix.wav";
  seed_audio(state, canonical, make_program(frames));
  MemoryCodec codecs(state);
  const auto canonical_analysis = analyze(codecs, canonical);

  MemoryWorkflowProvider provider(
      state, make_harsh_tonal_stem(frames), amt::separation::StemRole::tonal);
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider);

  auto request = workflow_request(canonical);
  assert(!request.evidence.source_specific_issue);

  std::string error;
  const auto result = amt::separation::evaluate_source_guided_workflow(
      orchestrator, codecs, canonical_analysis, request, error, workflow_config());
  assert(result.has_value());
  assert(error.empty());
  assert(provider.calls_ == 1);
  assert(result->source_estimates_analyzed);
  assert(result->evidence.source_specific_issue);
  assert(has_issue(*reinterpret_cast<const amt::separation::SourceIssueInferenceResult*>(
                       &amt::separation::SourceIssueInferenceResult{
                           .issues = result->issues,
                           .evidence = result->evidence,
                           .measurement_confidence = result->measurement_confidence}),
                   amt::separation::StemRole::tonal,
                   amt::separation::SourceGuidedIssueType::harshness));
  assert(result->guidance.decision.mode ==
         amt::separation::SeparationMode::source_guided_stereo);
  assert(!result->evidence.reconstruction_required_for_full_repair);
}

void test_diagnostic_separation_with_no_supported_issue_stays_stereo() {
  const auto root = test_root("workflow-stereo");
  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U * 4U;
  const auto canonical = root / "mix.wav";
  seed_audio(state, canonical, make_program(frames));
  MemoryCodec codecs(state);
  const auto canonical_analysis = analyze(codecs, canonical);

  MemoryWorkflowProvider provider(
      state, make_quiet_bass_stem(frames), amt::separation::StemRole::bass);
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider);

  std::string error;
  const auto result = amt::separation::evaluate_source_guided_workflow(
      orchestrator, codecs, canonical_analysis, workflow_request(canonical), error,
      workflow_config());
  assert(result.has_value());
  assert(error.empty());
  assert(provider.calls_ == 1);
  assert(result->source_estimates_analyzed);
  assert(result->issues.empty());
  assert(!result->evidence.source_specific_issue);
  assert(result->guidance.decision.mode ==
         amt::separation::SeparationMode::stereo_mastering);
}

void test_unavailable_provider_is_normal_stereo_fallback() {
  const auto root = test_root("workflow-provider-unavailable");
  auto state = std::make_shared<MemoryCodecState>();
  constexpr std::size_t frames = 48000U * 4U;
  const auto canonical = root / "mix.wav";
  seed_audio(state, canonical, make_program(frames));
  MemoryCodec codecs(state);
  const auto canonical_analysis = analyze(codecs, canonical);

  MemoryWorkflowProvider provider(
      state, make_harsh_tonal_stem(frames), amt::separation::StemRole::tonal);
  provider.available_ = false;
  amt::separation::SourceGuidanceOrchestrator orchestrator(provider);

  std::string error;
  const auto result = amt::separation::evaluate_source_guided_workflow(
      orchestrator, codecs, canonical_analysis, workflow_request(canonical), error,
      workflow_config());
  assert(result.has_value());
  assert(error.empty());
  assert(provider.calls_ == 0);
  assert(!result->guidance.separation.has_value());
  assert(!result->source_estimates_analyzed);
  assert(result->guidance.decision.mode ==
         amt::separation::SeparationMode::stereo_mastering);
}

}  // namespace

int main() {
  test_diagnostic_separation_requires_post_separation_evidence_for_mode1();
  test_diagnostic_separation_with_no_supported_issue_stays_stereo();
  test_unavailable_provider_is_normal_stereo_fallback();
  return 0;
}
