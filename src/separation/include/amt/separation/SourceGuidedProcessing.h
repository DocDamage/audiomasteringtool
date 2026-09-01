#pragma once

#include <optional>
#include <string>
#include <vector>

#include "amt/separation/Separation.h"

namespace amt::separation {

enum class SourceGuidedIssueType {
  excessive_level,
  harshness,
  muddiness,
  masking,
  excessive_width,
  transient_spike
};

enum class SourceGuidedAction {
  gain_riding,
  dynamic_eq_attenuation,
  stereo_width_reduction,
  transient_taming
};

struct SourceGuidedIssue {
  StemRole source{StemRole::unknown};
  SourceGuidedIssueType type{SourceGuidedIssueType::excessive_level};
  double severity{0.0};
  double confidence{0.0};
  std::optional<double> start_seconds;
  std::optional<double> end_seconds;
  std::optional<double> center_frequency_hz;
  std::optional<double> bandwidth_octaves;
  std::string evidence;
};

struct SourceGuidedProcessingConfig {
  double minimum_issue_confidence{0.65};
  double maximum_gain_ride_db{1.5};
  double maximum_dynamic_eq_cut_db{2.5};
  double maximum_width_reduction{0.30};
  double maximum_transient_taming{0.35};
  double minimum_center_frequency_hz{30.0};
  double maximum_center_frequency_hz{18000.0};
};

struct SourceGuidedIntervention {
  StemRole source{StemRole::unknown};
  SourceGuidedAction action{SourceGuidedAction::gain_riding};
  double amount{0.0};
  double confidence{0.0};
  std::optional<double> start_seconds;
  std::optional<double> end_seconds;
  std::optional<double> center_frequency_hz;
  std::optional<double> bandwidth_octaves;
  std::string rationale;
};

struct SourceGuidedProcessingPlan {
  bool operates_on_canonical_stereo{true};
  bool requires_reconstruction{false};
  std::vector<SourceGuidedIntervention> interventions;
  std::vector<std::string> skipped_reasons;
};

[[nodiscard]] SourceGuidedProcessingPlan build_source_guided_processing_plan(
    const SeparationDecision& decision,
    const std::vector<SourceGuidedIssue>& issues,
    const SourceGuidedProcessingConfig& config = {});

[[nodiscard]] std::string source_guided_action_name(SourceGuidedAction action);
[[nodiscard]] std::string source_guided_issue_name(SourceGuidedIssueType type);

}  // namespace amt::separation
