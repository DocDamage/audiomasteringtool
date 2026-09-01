#include <cassert>
#include <cmath>
#include <vector>

#include "amt/separation/SourceControlEnvelope.h"

namespace {

bool near(const double a, const double b, const double tolerance = 1.0e-6) {
  return std::abs(a - b) <= tolerance;
}

amt::separation::SourceGuidedIntervention vocal_gain_intervention() {
  return {.source = amt::separation::StemRole::vocals,
          .action = amt::separation::SourceGuidedAction::gain_riding,
          .amount = -1.5,
          .confidence = 0.82,
          .start_seconds = 1.0,
          .end_seconds = 3.0,
          .rationale = "Synthetic source-guided gain ride."};
}

void test_activity_lookup_interpolates_and_stops_at_envelope_end() {
  const amt::separation::SourceControlEnvelope envelope{
      .source = amt::separation::StemRole::vocals,
      .sample_rate = 1000,
      .hop_frames = 100U,
      .source_confidence = 0.90,
      .activity = {0.0F, 0.5F, 1.0F}};

  assert(near(amt::separation::source_activity_at_frame(envelope, 0), 0.0));
  assert(near(amt::separation::source_activity_at_frame(envelope, 50), 0.25));
  assert(near(amt::separation::source_activity_at_frame(envelope, 100), 0.5));
  assert(near(amt::separation::source_activity_at_frame(envelope, 150), 0.75));
  assert(near(amt::separation::source_activity_at_frame(envelope, 250), 1.0));
  assert(near(amt::separation::source_activity_at_frame(envelope, 300), 0.0));
  assert(near(amt::separation::source_activity_at_frame(envelope, -1), 0.0));
}

void test_binding_chooses_highest_confidence_matching_source() {
  amt::separation::SourceGuidedProcessingPlan processing;
  processing.interventions = {vocal_gain_intervention(),
                              {.source = amt::separation::StemRole::bass,
                               .action = amt::separation::SourceGuidedAction::dynamic_eq_attenuation,
                               .amount = -1.0,
                               .confidence = 0.80,
                               .center_frequency_hz = 120.0,
                               .rationale = "Synthetic bass control."}};

  std::vector<amt::separation::SourceControlEnvelope> envelopes{
      {.source = amt::separation::StemRole::vocals,
       .sample_rate = 48000,
       .hop_frames = 960U,
       .source_confidence = 0.60,
       .activity = {0.2F, 0.3F}},
      {.source = amt::separation::StemRole::vocals,
       .sample_rate = 48000,
       .hop_frames = 960U,
       .source_confidence = 0.95,
       .activity = {0.8F, 0.9F}}};

  const auto plan = amt::separation::bind_source_guided_controls(
      processing, std::move(envelopes));
  assert(plan.operates_on_canonical_stereo);
  assert(plan.bindings.size() == 1U);
  assert(plan.bindings.front().envelope_index == 1U);
  assert(plan.bindings.front().intervention.source == amt::separation::StemRole::vocals);
  assert(!plan.skipped_reasons.empty());
}

void test_control_amount_respects_time_window_and_sample_rate_mapping() {
  amt::separation::SourceGuidedProcessingPlan processing;
  processing.interventions = {vocal_gain_intervention()};
  std::vector<amt::separation::SourceControlEnvelope> envelopes{{
      .source = amt::separation::StemRole::vocals,
      .sample_rate = 1000,
      .hop_frames = 1000U,
      .source_confidence = 0.95,
      .activity = {0.0F, 1.0F, 0.5F, 0.0F}}};
  const auto plan = amt::separation::bind_source_guided_controls(
      processing, std::move(envelopes));
  assert(plan.bindings.size() == 1U);
  const auto& binding = plan.bindings.front();

  assert(near(amt::separation::controlled_intervention_amount_at_frame(
                  plan, binding, 24000, 48000),
              0.0));
  assert(near(amt::separation::controlled_intervention_amount_at_frame(
                  plan, binding, 48000, 48000),
              -1.5));
  assert(near(amt::separation::controlled_intervention_amount_at_frame(
                  plan, binding, 72000, 48000),
              -1.125));
  assert(near(amt::separation::controlled_intervention_amount_at_frame(
                  plan, binding, 120000, 48000),
              -0.375));
  assert(near(amt::separation::controlled_intervention_amount_at_frame(
                  plan, binding, 144000, 48000),
              0.0));
}

void test_noncanonical_processing_plan_is_rejected() {
  amt::separation::SourceGuidedProcessingPlan processing;
  processing.operates_on_canonical_stereo = false;
  processing.requires_reconstruction = true;
  processing.interventions = {vocal_gain_intervention()};

  const auto plan = amt::separation::bind_source_guided_controls(
      processing,
      {{.source = amt::separation::StemRole::vocals,
        .sample_rate = 48000,
        .hop_frames = 960U,
        .source_confidence = 0.95,
        .activity = {1.0F}}});
  assert(plan.operates_on_canonical_stereo);
  assert(plan.bindings.empty());
  assert(!plan.skipped_reasons.empty());
}

}  // namespace

int main() {
  test_activity_lookup_interpolates_and_stops_at_envelope_end();
  test_binding_chooses_highest_confidence_matching_source();
  test_control_amount_respects_time_window_and_sample_rate_mapping();
  test_noncanonical_processing_plan_is_rejected();
  return 0;
}
