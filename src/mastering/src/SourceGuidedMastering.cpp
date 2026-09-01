#include "amt/mastering/SourceGuidedMastering.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace amt::mastering {
namespace {

struct ScopedWorkDirectory {
  explicit ScopedWorkDirectory(std::filesystem::path value) : path(std::move(value)) {}

  ScopedWorkDirectory(const ScopedWorkDirectory&) = delete;
  ScopedWorkDirectory& operator=(const ScopedWorkDirectory&) = delete;

  ScopedWorkDirectory(ScopedWorkDirectory&& other) noexcept : path(std::move(other.path)) {
    other.path.clear();
  }

  ScopedWorkDirectory& operator=(ScopedWorkDirectory&& other) noexcept {
    if (this == &other) return *this;
    cleanup();
    path = std::move(other.path);
    other.path.clear();
    return *this;
  }

  ~ScopedWorkDirectory() { cleanup(); }

  void cleanup() noexcept {
    if (path.empty()) return;
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
    path.clear();
  }

  std::filesystem::path path;
};

struct GuidedAttemptResult {
  MasteringRenderPair masters;
  std::size_t applied_bindings{0U};
};

std::atomic<std::uint64_t> g_work_directory_counter{0U};

[[nodiscard]] bool cancelled(const amt::core::CancellationToken* cancellation) noexcept {
  return cancellation != nullptr && cancellation->is_cancelled();
}

void append_warnings(std::vector<std::string>& destination,
                     const std::vector<std::string>& source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

[[nodiscard]] std::optional<ScopedWorkDirectory> create_work_directory(
    const std::filesystem::path& output_directory,
    std::string& error) {
  std::error_code directory_error;
  std::filesystem::create_directories(output_directory, directory_error);
  if (directory_error) {
    error = "unable to create source-guided mastering output directory: " +
            directory_error.message();
    return std::nullopt;
  }

  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto sequence = g_work_directory_counter.fetch_add(1U, std::memory_order_relaxed);
  const auto path = output_directory /
      (".amt-source-guided-work-" + std::to_string(ticks) + "-" +
       std::to_string(sequence));
  std::filesystem::create_directory(path, directory_error);
  if (directory_error) {
    error = "unable to create source-guided mastering work directory: " +
            directory_error.message();
    return std::nullopt;
  }
  return std::optional<ScopedWorkDirectory>{std::in_place, path};
}

[[nodiscard]] std::optional<SourceGuidedMasteringRenderPair> render_mode0(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    const MasteringPlan& plan,
    const amt::separation::SeparationMode requested_mode,
    std::vector<std::string> warnings,
    std::string& error,
    const RenderSettings& render_settings,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress,
    const double progress_start = 0.0) {
  const auto rendered = render_mastering_plan(
      codecs, canonical_input, output_directory, source_analysis, plan, error,
      render_settings, cancellation, [&](const double value) {
        amt::core::report_progress(progress,
                                   progress_start + (1.0 - progress_start) * value);
      });
  if (!rendered) return std::nullopt;

  error.clear();
  return SourceGuidedMasteringRenderPair{
      .masters = *rendered,
      .source_guidance_applied = false,
      .requested_mode = requested_mode,
      .rendered_mode = amt::separation::SeparationMode::stereo_mastering,
      .applied_bindings = 0U,
      .warnings = std::move(warnings)};
}

[[nodiscard]] std::optional<GuidedAttemptResult> attempt_mode1(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    const amt::separation::SourceGuidanceResult& guidance,
    const amt::separation::SeparationDecision& guided_decision,
    const std::vector<amt::separation::SourceGuidedIssue>& issues,
    std::vector<std::string>& warnings,
    std::string& error,
    const SourceGuidedMasteringConfig& config,
    const RenderSettings& render_settings,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  if (!guidance.separation) {
    error = "source-guided stereo mastering requires validated source estimates";
    return std::nullopt;
  }

  const auto processing = amt::separation::build_source_guided_processing_plan(
      guided_decision, issues, config.processing);
  append_warnings(warnings, processing.skipped_reasons);
  if (!processing.operates_on_canonical_stereo || processing.requires_reconstruction ||
      processing.interventions.empty()) {
    error = "source-guided stereo mastering has no safe canonical-stereo interventions";
    return std::nullopt;
  }

  auto envelopes = amt::separation::build_source_control_envelopes(
      codecs, *guidance.separation, error, config.envelopes, cancellation,
      [&](const double value) {
        amt::core::report_progress(progress, value * 0.20);
      });
  if (!envelopes) return std::nullopt;
  if (cancelled(cancellation)) {
    error = "source-guided mastering cancelled";
    return std::nullopt;
  }

  auto controls = amt::separation::bind_source_guided_controls(
      processing, std::move(*envelopes));
  append_warnings(warnings, controls.skipped_reasons);
  if (!controls.operates_on_canonical_stereo || controls.bindings.empty()) {
    error = "source-guided stereo mastering has no usable source-control bindings";
    return std::nullopt;
  }

  auto work_directory = create_work_directory(output_directory, error);
  if (!work_directory) return std::nullopt;
  auto guided_name = canonical_input.stem();
  if (guided_name.empty()) guided_name = "source";
  guided_name += ".wav";
  const auto guided_path = work_directory->path / guided_name;

  const auto guided_render = amt::separation::render_source_guided_stereo(
      codecs, canonical_input, guided_path, controls, error, config.executor,
      cancellation, [&](const double value) {
        amt::core::report_progress(progress, 0.20 + value * 0.25);
      });
  if (!guided_render) return std::nullopt;
  if (!guided_render->canonical_program_path) {
    error = "source-guided stereo renderer did not preserve the canonical program path";
    return std::nullopt;
  }
  if (cancelled(cancellation)) {
    error = "source-guided mastering cancelled";
    return std::nullopt;
  }

  const auto guided_analysis = amt::analysis::analyze_file(
      codecs, guided_path, error, cancellation, [&](const double value) {
        amt::core::report_progress(progress, 0.45 + value * 0.10);
      });
  if (!guided_analysis) return std::nullopt;
  if (cancelled(cancellation)) {
    error = "source-guided mastering cancelled";
    return std::nullopt;
  }

  // Mode 1 changes the canonical-stereo-derived program before the normal mastering
  // graph. Re-plan from that post-guidance signal so the downstream graph does not
  // repeat corrections that source guidance has already made. The untouched source
  // analysis is still passed to render_mastering_plan solely for Original A/B
  // loudness matching.
  const auto guided_plan = plan_mastering(*guided_analysis);
  const auto mastered = render_mastering_plan(
      codecs, guided_path, output_directory, source_analysis, guided_plan, error,
      render_settings, cancellation, [&](const double value) {
        amt::core::report_progress(progress, 0.55 + value * 0.45);
      });
  if (!mastered) return std::nullopt;

  return GuidedAttemptResult{.masters = *mastered,
                             .applied_bindings = guided_render->applied_bindings};
}

}  // namespace

std::optional<SourceGuidedMasteringRenderPair>
render_mastering_plan_with_source_guidance(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    const MasteringPlan& plan,
    const amt::separation::SourceGuidanceResult& guidance,
    const std::vector<amt::separation::SourceGuidedIssue>& issues,
    std::string& error,
    const SourceGuidedMasteringConfig& config,
    const RenderSettings& render_settings,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  std::vector<std::string> warnings = guidance.warnings;
  const auto requested_mode = guidance.decision.mode;

  if (cancelled(cancellation)) {
    error = "source-guided mastering cancelled";
    return std::nullopt;
  }

  if (requested_mode == amt::separation::SeparationMode::stereo_mastering) {
    return render_mode0(codecs, canonical_input, output_directory, source_analysis,
                        plan, requested_mode, std::move(warnings), error,
                        render_settings, cancellation, progress);
  }

  auto guided_decision = guidance.decision;
  if (requested_mode == amt::separation::SeparationMode::stem_reconstruction) {
    warnings.emplace_back(
        "Mode 2 was requested, but the reconstruction renderer is not implemented; "
        "attempting bounded Mode 1 processing on the canonical stereo mix instead");
    guided_decision.mode = amt::separation::SeparationMode::source_guided_stereo;
  }

  const auto guided = attempt_mode1(
      codecs, canonical_input, output_directory, source_analysis, guidance,
      guided_decision, issues, warnings, error, config, render_settings,
      cancellation, progress);
  if (guided) {
    error.clear();
    return SourceGuidedMasteringRenderPair{
        .masters = guided->masters,
        .source_guidance_applied = true,
        .requested_mode = requested_mode,
        .rendered_mode = amt::separation::SeparationMode::source_guided_stereo,
        .applied_bindings = guided->applied_bindings,
        .warnings = std::move(warnings)};
  }

  if (cancelled(cancellation)) {
    if (error.empty()) error = "source-guided mastering cancelled";
    return std::nullopt;
  }
  if (!config.fallback_to_stereo_mastering) return std::nullopt;

  const std::string guided_error = error.empty()
      ? std::string("source-guided stereo processing was unavailable")
      : error;
  warnings.emplace_back(
      "Source-guided stereo processing fell back to canonical stereo mastering: " +
      guided_error);
  error.clear();
  return render_mode0(codecs, canonical_input, output_directory, source_analysis,
                      plan, requested_mode, std::move(warnings), error,
                      render_settings, cancellation, progress, 0.55);
}

}  // namespace amt::mastering
