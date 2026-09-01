#include "amt/separation/SourceIssueInference.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace amt::separation {
namespace {

struct AnalyzedSource {
  StemRole role{StemRole::unknown};
  double artifact_confidence{0.0};
  double effective_confidence{0.0};
  amt::analysis::Phase1AnalysisReport report;
};

[[nodiscard]] double clamp01(const double value) {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] double ramp(const double value,
                          const double threshold,
                          const double ceiling) {
  if (!std::isfinite(value) || value <= threshold) return 0.0;
  if (ceiling <= threshold) return 1.0;
  return clamp01((value - threshold) / (ceiling - threshold));
}

[[nodiscard]] bool cancelled(const amt::core::CancellationToken* cancellation) noexcept {
  return cancellation != nullptr && cancellation->is_cancelled();
}

[[nodiscard]] double duration_seconds(const amt::codec::AudioMetadata& metadata) {
  if (metadata.sample_rate <= 0 || metadata.frames <= 0) return 0.0;
  return static_cast<double>(metadata.frames) /
         static_cast<double>(metadata.sample_rate);
}

[[nodiscard]] double band_energy(const amt::analysis::SpectrumMetrics& spectrum,
                                 const double low_hz,
                                 const double high_hz) {
  double total = 0.0;
  for (const auto& band : spectrum.bands) {
    if (band.high_hz <= low_hz || band.low_hz >= high_hz) continue;
    const double width = std::max(0.0, band.high_hz - band.low_hz);
    if (width <= 0.0) continue;
    const double overlap = std::max(
        0.0, std::min(high_hz, band.high_hz) - std::max(low_hz, band.low_hz));
    total += band.energy_ratio * clamp01(overlap / width);
  }
  return clamp01(total);
}

[[nodiscard]] bool percussive_role(const StemRole role) noexcept {
  return role == StemRole::drums || role == StemRole::kick ||
         role == StemRole::snare || role == StemRole::percussion;
}

[[nodiscard]] bool tonal_role(const StemRole role) noexcept {
  return role == StemRole::vocals || role == StemRole::bass ||
         role == StemRole::other || role == StemRole::tonal;
}

[[nodiscard]] double loudness_power_share(const double source_lufs,
                                          const double program_lufs) {
  if (!std::isfinite(source_lufs) || !std::isfinite(program_lufs) ||
      source_lufs <= -190.0 || program_lufs <= -190.0) {
    return 0.0;
  }
  return std::clamp(std::pow(10.0, (source_lufs - program_lufs) / 10.0), 0.0, 2.0);
}

[[nodiscard]] double measurement_confidence(
    const amt::analysis::Phase1AnalysisReport& report,
    const SourceIssueInferenceConfig& config) {
  const double duration = duration_seconds(report.metadata);
  const double duration_score = ramp(duration, config.minimum_duration_seconds, 12.0);
  const double loudness_score = ramp(report.loudness.integrated_lufs,
                                     config.minimum_source_loudness_lufs, -30.0);
  return clamp01(0.82 + 0.10 * duration_score + 0.08 * loudness_score);
}

[[nodiscard]] double issue_confidence(const double source_confidence,
                                      const double severity) {
  return clamp01(source_confidence * (0.78 + 0.22 * clamp01(severity)));
}

[[nodiscard]] std::string percent(const double value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(1) << clamp01(value) * 100.0 << '%';
  return output.str();
}

[[nodiscard]] std::string number(const double value, const int precision = 2) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(precision) << value;
  return output.str();
}

void maybe_add_issue(std::vector<SourceGuidedIssue>& issues,
                     SourceGuidedIssue issue,
                     const SourceIssueInferenceConfig& config) {
  issue.severity = clamp01(issue.severity);
  issue.confidence = clamp01(issue.confidence);
  if (issue.severity <= 0.0 || issue.confidence < config.minimum_issue_confidence) return;
  issues.push_back(std::move(issue));
}

[[nodiscard]] double activity_overlap(
    const amt::analysis::LoudnessMetrics& first,
    const amt::analysis::LoudnessMetrics& second) {
  if (first.timeline.empty() || second.timeline.empty()) return 0.0;

  const double first_threshold = std::max(-60.0, first.max_short_term_lufs - 18.0);
  const double second_threshold = std::max(-60.0, second.max_short_term_lufs - 18.0);

  std::size_t second_index = 0U;
  std::size_t matched = 0U;
  std::size_t first_active = 0U;
  std::size_t second_active = 0U;
  std::size_t both_active = 0U;

  for (const auto& point : first.timeline) {
    while (second_index + 1U < second.timeline.size() &&
           std::abs(second.timeline[second_index + 1U].time_seconds - point.time_seconds) <=
               std::abs(second.timeline[second_index].time_seconds - point.time_seconds)) {
      ++second_index;
    }
    const auto& other = second.timeline[second_index];
    if (std::abs(other.time_seconds - point.time_seconds) > 0.50) continue;

    const bool a = point.short_term_lufs >= first_threshold;
    const bool b = other.short_term_lufs >= second_threshold;
    ++matched;
    if (a) ++first_active;
    if (b) ++second_active;
    if (a && b) ++both_active;
  }

  if (matched == 0U || first_active == 0U || second_active == 0U) return 0.0;
  return clamp01(static_cast<double>(both_active) /
                 static_cast<double>(std::min(first_active, second_active)));
}

void infer_single_source_issues(
    const AnalyzedSource& source,
    const amt::analysis::Phase1AnalysisReport& program,
    const SourceIssueInferenceConfig& config,
    std::vector<SourceGuidedIssue>& issues) {
  const auto& report = source.report;
  const auto role = source.role;
  const double source_confidence = source.effective_confidence;

  // Dominance is only treated as excessive when the source estimate itself is
  // nearly as loud as the full program both globally and at its loudest section.
  const double share = loudness_power_share(report.loudness.integrated_lufs,
                                            program.loudness.integrated_lufs);
  const double peak_gap = report.loudness.max_short_term_lufs -
                          program.loudness.max_short_term_lufs;
  if (role != StemRole::other && share > config.excessive_level_power_share &&
      peak_gap > -0.70) {
    const double share_severity = ramp(share, config.excessive_level_power_share, 1.20);
    const double peak_severity = ramp(peak_gap, -0.70, 0.30);
    const double severity = 0.65 * share_severity + 0.35 * peak_severity;
    maybe_add_issue(
        issues,
        {.source = role,
         .type = SourceGuidedIssueType::excessive_level,
         .severity = severity,
         .confidence = issue_confidence(source_confidence, severity),
         .evidence = stem_role_name(role) + " estimate carries " + percent(share) +
                     " of program-equivalent loudness power and reaches within " +
                     number(std::abs(peak_gap), 2) +
                     " LU of the program's maximum short-term loudness."},
        config);
  }

  const double presence = band_energy(report.spectrum, 2000.0, 6000.0);
  const double air = band_energy(report.spectrum, 6000.0, 20000.0);
  const bool percussive = percussive_role(role);
  const double harsh_metric = percussive
      ? std::max(presence, air * 0.85)
      : presence + 0.20 * air;
  const double harsh_threshold = percussive
      ? config.harsh_percussive_energy_ratio
      : config.harsh_presence_energy_ratio;
  if ((percussive || tonal_role(role)) && harsh_metric > harsh_threshold) {
    const double severity = ramp(harsh_metric, harsh_threshold,
                                 percussive ? 0.82 : 0.72);
    const bool high_band_dominates = air > presence * 1.15;
    maybe_add_issue(
        issues,
        {.source = role,
         .type = SourceGuidedIssueType::harshness,
         .severity = severity,
         .confidence = issue_confidence(source_confidence, severity),
         .center_frequency_hz = high_band_dominates ? 8500.0 : 3500.0,
         .bandwidth_octaves = high_band_dominates ? 0.85 : 0.70,
         .evidence = stem_role_name(role) + " estimate has " + percent(presence) +
                     " energy in 2–6 kHz and " + percent(air) +
                     " above 6 kHz; the harshness attribution comes from the separated estimate."},
        config);
  }

  const double mud = band_energy(report.spectrum, 250.0, 500.0);
  const double mud_threshold = role == StemRole::bass
      ? config.bass_muddiness_energy_ratio
      : config.muddiness_energy_ratio;
  if ((tonal_role(role) || role == StemRole::drums) && mud > mud_threshold) {
    const double severity = ramp(mud, mud_threshold, 0.62);
    maybe_add_issue(
        issues,
        {.source = role,
         .type = SourceGuidedIssueType::muddiness,
         .severity = severity,
         .confidence = issue_confidence(source_confidence, severity),
         .center_frequency_hz = 350.0,
         .bandwidth_octaves = 1.05,
         .evidence = stem_role_name(role) + " estimate places " + percent(mud) +
                     " of measured spectral energy in the 250–500 Hz mud band."},
        config);
  }

  if (report.metadata.channels >= 2) {
    if (role == StemRole::bass &&
        report.stereo.low_band_width > config.bass_low_band_width) {
      const double severity = ramp(report.stereo.low_band_width,
                                   config.bass_low_band_width, 0.48);
      maybe_add_issue(
          issues,
          {.source = role,
           .type = SourceGuidedIssueType::excessive_width,
           .severity = severity,
           .confidence = issue_confidence(source_confidence, severity),
           .evidence = "Bass estimate low-band side-energy ratio is " +
                       percent(report.stereo.low_band_width) +
                       ", providing direct source evidence for low-end width reduction."},
          config);
    } else if (role != StemRole::bass &&
               report.stereo.mid_band_width > config.general_mid_band_width &&
               (report.stereo.negative_correlation_window_fraction > 0.05 ||
                report.stereo.mono_fold_down_delta_db < -1.0)) {
      const double width_severity = ramp(report.stereo.mid_band_width,
                                         config.general_mid_band_width, 0.72);
      const double phase_severity = std::max(
          ramp(report.stereo.negative_correlation_window_fraction, 0.05, 0.30),
          ramp(-report.stereo.mono_fold_down_delta_db, 1.0, 6.0));
      const double severity = 0.65 * width_severity + 0.35 * phase_severity;
      maybe_add_issue(
          issues,
          {.source = role,
           .type = SourceGuidedIssueType::excessive_width,
           .severity = severity,
           .confidence = issue_confidence(source_confidence, severity),
           .evidence = stem_role_name(role) + " estimate has " +
                       percent(report.stereo.mid_band_width) +
                       " mid-band side energy with measurable mono/phase risk."},
          config);
    }
  }

  if (percussive &&
      report.loudness.crest_factor_db > config.transient_crest_factor_db &&
      report.loudness.sample_peak_dbfs > config.transient_peak_floor_dbfs) {
    const double crest_severity = ramp(report.loudness.crest_factor_db,
                                       config.transient_crest_factor_db, 21.0);
    const double peak_severity = ramp(report.loudness.sample_peak_dbfs,
                                      config.transient_peak_floor_dbfs, -0.25);
    const double severity = 0.70 * crest_severity + 0.30 * peak_severity;
    maybe_add_issue(
        issues,
        {.source = role,
         .type = SourceGuidedIssueType::transient_spike,
         .severity = severity,
         .confidence = issue_confidence(source_confidence, severity),
         .evidence = stem_role_name(role) + " estimate measures " +
                     number(report.loudness.crest_factor_db, 1) +
                     " dB crest factor with a " +
                     number(report.loudness.sample_peak_dbfs, 1) +
                     " dBFS sample peak."},
        config);
  }
}

void infer_low_frequency_masking(
    const std::vector<AnalyzedSource>& sources,
    const SourceIssueInferenceConfig& config,
    std::vector<SourceGuidedIssue>& issues) {
  const AnalyzedSource* bass = nullptr;
  const AnalyzedSource* percussion = nullptr;
  double percussion_score = 0.0;

  for (const auto& source : sources) {
    if (source.role == StemRole::bass &&
        (bass == nullptr || source.effective_confidence > bass->effective_confidence)) {
      bass = &source;
      continue;
    }
    if (source.role != StemRole::kick && source.role != StemRole::drums) continue;
    const double low = band_energy(source.report.spectrum, 20.0, 250.0);
    const double score = low * source.effective_confidence *
                         (source.role == StemRole::kick ? 1.10 : 1.0);
    if (score > percussion_score) {
      percussion = &source;
      percussion_score = score;
    }
  }

  if (bass == nullptr || percussion == nullptr) return;

  const double bass_low = band_energy(bass->report.spectrum, 20.0, 250.0);
  const double percussion_low = band_energy(percussion->report.spectrum, 20.0, 250.0);
  if (bass_low <= config.masking_bass_low_energy_ratio ||
      percussion_low <= config.masking_percussion_low_energy_ratio) {
    return;
  }

  const double overlap = activity_overlap(bass->report.loudness,
                                          percussion->report.loudness);
  if (overlap <= config.masking_minimum_activity_overlap) return;

  const double bass_severity = ramp(bass_low,
                                    config.masking_bass_low_energy_ratio, 0.90);
  const double percussion_severity = ramp(
      percussion_low, config.masking_percussion_low_energy_ratio, 0.52);
  const double overlap_severity = ramp(
      overlap, config.masking_minimum_activity_overlap, 0.90);
  const double severity = 0.35 * bass_severity +
                          0.25 * percussion_severity +
                          0.40 * overlap_severity;
  const double source_confidence = std::min(bass->effective_confidence,
                                            percussion->effective_confidence);

  maybe_add_issue(
      issues,
      {.source = StemRole::bass,
       .type = SourceGuidedIssueType::masking,
       .severity = severity,
       .confidence = issue_confidence(source_confidence, severity),
       .center_frequency_hz = percussion->role == StemRole::kick ? 75.0 : 95.0,
       .bandwidth_octaves = 1.10,
       .evidence = "Separated bass and " + stem_role_name(percussion->role) +
                   " estimates overlap during " + percent(overlap) +
                   " of the smaller source's active analysis windows; low-band energy is " +
                   percent(bass_low) + " for bass and " + percent(percussion_low) +
                   " for " + stem_role_name(percussion->role) + "."},
      config);
}

void cap_issues_per_source(std::vector<SourceGuidedIssue>& issues,
                           const std::size_t maximum_per_source) {
  if (maximum_per_source == 0U) {
    issues.clear();
    return;
  }

  std::stable_sort(issues.begin(), issues.end(), [](const auto& first, const auto& second) {
    return first.severity * first.confidence > second.severity * second.confidence;
  });

  std::map<StemRole, std::size_t> counts;
  issues.erase(std::remove_if(issues.begin(), issues.end(), [&](const auto& issue) {
                 auto& count = counts[issue.source];
                 if (count >= maximum_per_source) return true;
                 ++count;
                 return false;
               }),
               issues.end());
}

}  // namespace

std::optional<SourceIssueInferenceResult> infer_source_guided_issues(
    amt::codec::ICodecService& codecs,
    const amt::analysis::Phase1AnalysisReport& canonical_analysis,
    const SeparationResult& separation,
    std::string& error,
    const SourceIssueInferenceConfig& config,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  SourceIssueInferenceResult result;

  if (canonical_analysis.metadata.sample_rate <= 0 ||
      canonical_analysis.metadata.frames < 0) {
    error = "source-issue inference received invalid canonical analysis metadata";
    return std::nullopt;
  }
  if (!std::isfinite(separation.overall_confidence) ||
      separation.overall_confidence < 0.0 || separation.overall_confidence > 1.0) {
    error = "source-issue inference received invalid separation confidence";
    return std::nullopt;
  }
  if (cancelled(cancellation)) {
    error = "source-issue inference cancelled";
    return std::nullopt;
  }

  std::map<StemRole, const SeparationArtifactReference*> selected_artifacts;
  for (const auto& artifact : separation.artifacts) {
    if (artifact.kind != CacheArtifactKind::stem_audio ||
        artifact.role == StemRole::unknown || artifact.path.empty() ||
        !std::isfinite(artifact.confidence)) {
      continue;
    }
    const auto existing = selected_artifacts.find(artifact.role);
    if (existing == selected_artifacts.end() ||
        artifact.confidence > existing->second->confidence) {
      selected_artifacts[artifact.role] = &artifact;
    }
  }

  if (selected_artifacts.empty()) {
    result.warnings.emplace_back(
        "source-issue inference skipped because no validated stem-audio estimates are available");
    amt::core::report_progress(progress, 1.0);
    return result;
  }

  const double program_duration = duration_seconds(canonical_analysis.metadata);
  std::vector<AnalyzedSource> sources;
  sources.reserve(selected_artifacts.size());
  std::size_t completed = 0U;

  for (const auto& [role, artifact] : selected_artifacts) {
    if (cancelled(cancellation)) {
      error = "source-issue inference cancelled";
      return std::nullopt;
    }

    const double declared_confidence = std::min(
        clamp01(separation.overall_confidence), clamp01(artifact->confidence));
    if (declared_confidence < config.minimum_stem_confidence) {
      result.warnings.emplace_back(
          stem_role_name(role) + " estimate skipped because model/artifact confidence is below the inference threshold");
      ++completed;
      amt::core::report_progress(progress,
                                 static_cast<double>(completed) /
                                     static_cast<double>(selected_artifacts.size()));
      continue;
    }

    std::string analysis_error;
    const auto report = amt::analysis::analyze_file(
        codecs, artifact->path, analysis_error, cancellation,
        [&](const double value) {
          const double base = static_cast<double>(completed) /
                              static_cast<double>(selected_artifacts.size());
          const double span = 1.0 / static_cast<double>(selected_artifacts.size());
          amt::core::report_progress(progress, base + value * span);
        });
    if (!report) {
      if (cancelled(cancellation)) {
        error = "source-issue inference cancelled";
        return std::nullopt;
      }
      result.warnings.emplace_back(
          stem_role_name(role) + " estimate could not be analyzed: " + analysis_error);
      ++completed;
      continue;
    }

    const double source_duration = duration_seconds(report->metadata);
    if (source_duration < config.minimum_duration_seconds) {
      result.warnings.emplace_back(
          stem_role_name(role) + " estimate skipped because it is too short for reliable source diagnosis");
      ++completed;
      continue;
    }
    if (report->loudness.integrated_lufs < config.minimum_source_loudness_lufs) {
      result.warnings.emplace_back(
          stem_role_name(role) + " estimate skipped because it is effectively inactive/silent");
      ++completed;
      continue;
    }
    if (program_duration > 0.0 &&
        std::abs(source_duration - program_duration) >
            std::max(config.maximum_duration_mismatch_seconds, program_duration * 0.002)) {
      result.warnings.emplace_back(
          stem_role_name(role) + " estimate skipped because its duration does not align with the canonical program");
      ++completed;
      continue;
    }

    const double measured_confidence = measurement_confidence(*report, config);
    sources.push_back({.role = role,
                       .artifact_confidence = artifact->confidence,
                       .effective_confidence = std::min(declared_confidence,
                                                        measured_confidence),
                       .report = *report});
    ++completed;
    amt::core::report_progress(progress,
                               static_cast<double>(completed) /
                                   static_cast<double>(selected_artifacts.size()));
  }

  for (const auto& source : sources) {
    infer_single_source_issues(source, canonical_analysis, config, result.issues);
  }
  infer_low_frequency_masking(sources, config, result.issues);
  cap_issues_per_source(result.issues, config.maximum_issues_per_source);

  if (!sources.empty()) {
    result.measurement_confidence = std::accumulate(
        sources.begin(), sources.end(), 0.0,
        [](const double total, const AnalyzedSource& source) {
          return total + source.effective_confidence;
        }) / static_cast<double>(sources.size());
  }

  result.evidence.model_confidence = clamp01(separation.overall_confidence);
  result.evidence.source_specific_issue = !result.issues.empty();
  result.evidence.reconstruction_required_for_full_repair = false;

  if (!result.issues.empty()) {
    double maximum_score = 0.0;
    double confidence_sum = 0.0;
    for (const auto& issue : result.issues) {
      maximum_score = std::max(maximum_score, issue.severity * issue.confidence);
      confidence_sum += issue.confidence;
    }
    result.evidence.expected_repair_benefit = clamp01(
        0.10 + 0.75 * maximum_score +
        0.04 * static_cast<double>(std::min<std::size_t>(result.issues.size() - 1U, 2U)));
    result.evidence.source_guidance_confidence = clamp01(
        confidence_sum / static_cast<double>(result.issues.size()));
    result.evidence.source_guided_stereo_sufficiency = 0.85;
  }

  amt::core::report_progress(progress, 1.0);
  return result;
}

}  // namespace amt::separation
