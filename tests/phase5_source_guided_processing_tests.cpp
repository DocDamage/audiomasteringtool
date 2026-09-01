#include <cassert>
#include <string>
#include <vector>

#include "amt/separation/SourceGuidedProcessing.h"

namespace {

amt::separation::SeparationDecision mode1_decision() {
  return {.mode = amt::separation::SeparationMode::source_guided_stereo,
          .expected_benefit = 0.75,
          .artifact_risk = 0.15,
          .confidence = 0.84,
          .reasons = {"synthetic Mode 1 decision"}};
}

void test_non_mode1_emits_no_interventions() {
  auto decision = mode1_decision();
  const std::vector<amt::separation::SourceGuidedIssue> issues{{
      .source = amt::separation::StemRole::vocals,
      .type = amt::separation::SourceGuidedIssueType::harshness,
      .severity = 0.8,
      .confidence = 0.9,
      .center_frequency_hz = 3500.0}};

  decision.mode = amt::separation::SeparationMode::stereo_mastering;
  auto plan = amt::separation::build_source_guided_processing_plan(decision, issues);
  assert(plan.interventions.empty());
  assert(!plan.skipped_reasons.empty());
  assert(plan.operates_on_canonical_stereo);
  assert(!plan.requires_reconstruction);

  decision.mode = amt::separation::SeparationMode::stem_reconstruction;
  plan = amt::separation::build_source_guided_processing_plan(decision, issues);
  assert(plan.interventions.empty());
  assert(!plan.skipped_reasons.empty());
  assert(plan.operates_on_canonical_stereo);
  assert(!plan.requires_reconstruction);
}

void test_tonal_intervention_is_bounded_and_explainable() {
  const auto plan = amt::separation::build_source_guided_processing_plan(
      mode1_decision(),
      {{.source = amt::separation::StemRole::vocals,
        .type = amt::separation::SourceGuidedIssueType::harshness,
        .severity = 1.0,
        .confidence = 0.95,
        .start_seconds = 12.0,
        .end_seconds = 18.0,
        .center_frequency_hz = 3600.0,
        .bandwidth_octaves = 0.6,
        .evidence = "Vocal estimate shows persistent upper-mid excess."}});

  assert(plan.operates_on_canonical_stereo);
  assert(!plan.requires_reconstruction);
  assert(plan.interventions.size() == 1U);
  const auto& intervention = plan.interventions.front();
  assert(intervention.action == amt::separation::SourceGuidedAction::dynamic_eq_attenuation);
  assert(intervention.source == amt::separation::StemRole::vocals);
  assert(intervention.amount <= 0.0 && intervention.amount >= -2.5);
  assert(intervention.center_frequency_hz == 3600.0);
  assert(intervention.bandwidth_octaves.has_value());
  assert(intervention.confidence == 0.84);
  assert(intervention.start_seconds == 12.0);
  assert(intervention.end_seconds == 18.0);
  assert(intervention.rationale.find("original stereo mix") != std::string::npos);
}

void test_all_intervention_types_respect_configured_caps() {
  amt::separation::SourceGuidedProcessingConfig config;
  config.maximum_gain_ride_db = 1.2;
  config.maximum_dynamic_eq_cut_db = 2.0;
  config.maximum_width_reduction = 0.25;
  config.maximum_transient_taming = 0.30;

  const auto plan = amt::separation::build_source_guided_processing_plan(
      mode1_decision(),
      {{.source = amt::separation::StemRole::bass,
        .type = amt::separation::SourceGuidedIssueType::excessive_level,
        .severity = 2.0,
        .confidence = 0.95},
       {.source = amt::separation::StemRole::drums,
        .type = amt::separation::SourceGuidedIssueType::masking,
        .severity = 2.0,
        .confidence = 0.95,
        .center_frequency_hz = 180.0},
       {.source = amt::separation::StemRole::other,
        .type = amt::separation::SourceGuidedIssueType::excessive_width,
        .severity = 2.0,
        .confidence = 0.95},
       {.source = amt::separation::StemRole::drums,
        .type = amt::separation::SourceGuidedIssueType::transient_spike,
        .severity = 2.0,
        .confidence = 0.95}},
      config);

  assert(plan.interventions.size() == 4U);
  assert(plan.interventions[0].action == amt::separation::SourceGuidedAction::gain_riding);
  assert(plan.interventions[0].amount == -1.2);
  assert(plan.interventions[1].action == amt::separation::SourceGuidedAction::dynamic_eq_attenuation);
  assert(plan.interventions[1].amount == -2.0);
  assert(plan.interventions[2].action == amt::separation::SourceGuidedAction::stereo_width_reduction);
  assert(plan.interventions[2].amount == 0.25);
  assert(plan.interventions[3].action == amt::separation::SourceGuidedAction::transient_taming);
  assert(plan.interventions[3].amount == 0.30);
}

void test_invalid_or_uncertain_issues_are_skipped() {
  const auto plan = amt::separation::build_source_guided_processing_plan(
      mode1_decision(),
      {{.source = amt::separation::StemRole::unknown,
        .type = amt::separation::SourceGuidedIssueType::excessive_level,
        .severity = 0.8,
        .confidence = 0.9},
       {.source = amt::separation::StemRole::vocals,
        .type = amt::separation::SourceGuidedIssueType::harshness,
        .severity = 0.8,
        .confidence = 0.4,
        .center_frequency_hz = 3500.0},
       {.source = amt::separation::StemRole::bass,
        .type = amt::separation::SourceGuidedIssueType::muddiness,
        .severity = 0.8,
        .confidence = 0.9,
        .center_frequency_hz = 10.0},
       {.source = amt::separation::StemRole::drums,
        .type = amt::separation::SourceGuidedIssueType::transient_spike,
        .severity = 0.8,
        .confidence = 0.9,
        .start_seconds = 4.0},
       {.source = amt::separation::StemRole::other,
        .type = amt::separation::SourceGuidedIssueType::excessive_width,
        .severity = 0.0,
        .confidence = 0.9}});

  assert(plan.interventions.empty());
  assert(plan.skipped_reasons.size() == 5U);
}

void test_names_are_stable() {
  assert(amt::separation::source_guided_action_name(
             amt::separation::SourceGuidedAction::gain_riding) == "bounded gain riding");
  assert(amt::separation::source_guided_issue_name(
             amt::separation::SourceGuidedIssueType::masking) == "masking");
}

}  // namespace

int main() {
  test_non_mode1_emits_no_interventions();
  test_tonal_intervention_is_bounded_and_explainable();
  test_all_intervention_types_respect_configured_caps();
  test_invalid_or_uncertain_issues_are_skipped();
  test_names_are_stable();
  return 0;
}
