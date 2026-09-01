#include "amt/separation/SourceGuidedProcessing.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace amt::separation {
namespace {

[[nodiscard]] double clamp01(const double value) {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] bool finite_optional(const std::optional<double>& value) {
  return !value || std::isfinite(*value);
}

[[nodiscard]] bool valid_time_window(const SourceGuidedIssue& issue) {
  if (!finite_optional(issue.start_seconds) || !finite_optional(issue.end_seconds)) return false;
  if (issue.start_seconds.has_value() != issue.end_seconds.has_value()) return false;
  if (!issue.start_seconds) return true;
  return *issue.start_seconds >= 0.0 && *issue.end_seconds > *issue.start_seconds;
}

[[nodiscard]] bool requires_frequency(const SourceGuidedIssueType type) {
  return type == SourceGuidedIssueType::harshness ||
         type == SourceGuidedIssueType::muddiness ||
         type == SourceGuidedIssueType::masking;
}

[[nodiscard]] double default_bandwidth_octaves(const SourceGuidedIssueType type) {
  switch (type) {
    case SourceGuidedIssueType::harshness: return 0.70;
    case SourceGuidedIssueType::muddiness: return 1.10;
    case SourceGuidedIssueType::masking: return 0.90;
    case SourceGuidedIssueType::excessive_level:
    case SourceGuidedIssueType::excessive_width:
    case SourceGuidedIssueType::transient_spike:
      return 1.00;
  }
  return 1.00;
}

[[nodiscard]] std::string describe_source(const StemRole source) {
  const auto name = stem_role_name(source);
  return name == "unknown" ? std::string("source") : name;
}

[[nodiscard]] std::string rationale_for(const SourceGuidedIssue& issue,
                                        const SourceGuidedAction action) {
  std::ostringstream output;
  output << "Use the " << describe_source(issue.source)
         << " estimate only as control evidence for "
         << source_guided_action_name(action)
         << " on the original stereo mix.";
  if (!issue.evidence.empty()) output << ' ' << issue.evidence;
  return output.str();
}

}  // namespace

SourceGuidedProcessingPlan build_source_guided_processing_plan(
    const SeparationDecision& decision,
    const std::vector<SourceGuidedIssue>& issues,
    const SourceGuidedProcessingConfig& config) {
  SourceGuidedProcessingPlan plan;
  plan.operates_on_canonical_stereo = true;
  plan.requires_reconstruction = false;

  if (decision.mode != SeparationMode::source_guided_stereo) {
    plan.skipped_reasons.emplace_back(
        decision.mode == SeparationMode::stem_reconstruction
            ? "source-guided stereo interventions were not emitted because Mode 2 was selected"
            : "source-guided stereo interventions were not emitted because Mode 1 was not selected");
    return plan;
  }

  const double minimum_confidence = clamp01(config.minimum_issue_confidence);
  const double maximum_gain_ride_db = std::max(0.0, config.maximum_gain_ride_db);
  const double maximum_dynamic_eq_cut_db = std::max(0.0, config.maximum_dynamic_eq_cut_db);
  const double maximum_width_reduction = clamp01(config.maximum_width_reduction);
  const double maximum_transient_taming = clamp01(config.maximum_transient_taming);
  const double minimum_frequency = std::max(1.0, config.minimum_center_frequency_hz);
  const double maximum_frequency = std::max(minimum_frequency, config.maximum_center_frequency_hz);

  for (const auto& issue : issues) {
    if (issue.source == StemRole::unknown) {
      plan.skipped_reasons.emplace_back("source-guided issue skipped because its source role is unknown");
      continue;
    }
    if (!std::isfinite(issue.severity) || !std::isfinite(issue.confidence)) {
      plan.skipped_reasons.emplace_back("source-guided issue skipped because severity/confidence is not finite");
      continue;
    }
    const double severity = clamp01(issue.severity);
    const double confidence = std::min(clamp01(issue.confidence), clamp01(decision.confidence));
    if (severity <= 0.0) {
      plan.skipped_reasons.emplace_back("source-guided issue skipped because severity is zero");
      continue;
    }
    if (confidence < minimum_confidence) {
      plan.skipped_reasons.emplace_back("source-guided issue skipped because confidence is below the intervention threshold");
      continue;
    }
    if (!valid_time_window(issue)) {
      plan.skipped_reasons.emplace_back("source-guided issue skipped because its time window is invalid");
      continue;
    }

    if (requires_frequency(issue.type)) {
      if (!issue.center_frequency_hz || !std::isfinite(*issue.center_frequency_hz) ||
          *issue.center_frequency_hz < minimum_frequency ||
          *issue.center_frequency_hz > maximum_frequency) {
        plan.skipped_reasons.emplace_back("source-guided tonal issue skipped because its center frequency is invalid");
        continue;
      }
    }

    SourceGuidedIntervention intervention;
    intervention.source = issue.source;
    intervention.confidence = confidence;
    intervention.start_seconds = issue.start_seconds;
    intervention.end_seconds = issue.end_seconds;

    switch (issue.type) {
      case SourceGuidedIssueType::excessive_level:
        intervention.action = SourceGuidedAction::gain_riding;
        intervention.amount = -severity * maximum_gain_ride_db;
        break;
      case SourceGuidedIssueType::harshness:
      case SourceGuidedIssueType::muddiness:
      case SourceGuidedIssueType::masking:
        intervention.action = SourceGuidedAction::dynamic_eq_attenuation;
        intervention.amount = -severity * maximum_dynamic_eq_cut_db;
        intervention.center_frequency_hz = issue.center_frequency_hz;
        intervention.bandwidth_octaves = std::clamp(
            issue.bandwidth_octaves.value_or(default_bandwidth_octaves(issue.type)), 0.20, 2.00);
        break;
      case SourceGuidedIssueType::excessive_width:
        intervention.action = SourceGuidedAction::stereo_width_reduction;
        intervention.amount = severity * maximum_width_reduction;
        break;
      case SourceGuidedIssueType::transient_spike:
        intervention.action = SourceGuidedAction::transient_taming;
        intervention.amount = severity * maximum_transient_taming;
        break;
    }

    intervention.rationale = rationale_for(issue, intervention.action);
    plan.interventions.push_back(std::move(intervention));
  }
  return plan;
}

std::string source_guided_action_name(const SourceGuidedAction action) {
  switch (action) {
    case SourceGuidedAction::gain_riding: return "bounded gain riding";
    case SourceGuidedAction::dynamic_eq_attenuation: return "bounded dynamic-EQ attenuation";
    case SourceGuidedAction::stereo_width_reduction: return "bounded stereo-width reduction";
    case SourceGuidedAction::transient_taming: return "bounded transient taming";
  }
  return "bounded source-guided processing";
}

std::string source_guided_issue_name(const SourceGuidedIssueType type) {
  switch (type) {
    case SourceGuidedIssueType::excessive_level: return "excessive_level";
    case SourceGuidedIssueType::harshness: return "harshness";
    case SourceGuidedIssueType::muddiness: return "muddiness";
    case SourceGuidedIssueType::masking: return "masking";
    case SourceGuidedIssueType::excessive_width: return "excessive_width";
    case SourceGuidedIssueType::transient_spike: return "transient_spike";
  }
  return "unknown";
}

}  // namespace amt::separation
