#include <cassert>

#include "amt/separation/ReconstructionArtifactEvaluator.h"

namespace {

void test_clean_reconstruction_scores_low_risk() {
  const amt::separation::ReconstructionComparisonMetrics metrics{
      .sample_count = 48000U * 2U * 30U,
      .residual_ratio = 0.005,
      .correlation = 0.9998,
      .transient_mismatch_ratio = 0.008,
      .high_frequency_mismatch_ratio = 0.010,
      .measurement_confidence = 0.90};

  const auto assessment = amt::separation::assess_reconstruction_comparison(metrics, 0.96);
  assert(assessment.overall_risk < 0.12);
  assert(assessment.confidence > 0.80);
  assert(!assessment.evidence.empty());
}

void test_damaged_reconstruction_scores_high_risk() {
  const amt::separation::ReconstructionComparisonMetrics metrics{
      .sample_count = 48000U * 2U * 30U,
      .residual_ratio = 0.45,
      .correlation = 0.72,
      .transient_mismatch_ratio = 0.55,
      .high_frequency_mismatch_ratio = 0.65,
      .measurement_confidence = 0.90};

  const auto assessment = amt::separation::assess_reconstruction_comparison(metrics, 0.96);
  assert(assessment.overall_risk > 0.60);
  assert(assessment.confidence > 0.80);
}

void test_low_confidence_cannot_look_certain() {
  const amt::separation::ReconstructionComparisonMetrics metrics{
      .sample_count = 1024U,
      .residual_ratio = 0.01,
      .correlation = 0.999,
      .transient_mismatch_ratio = 0.01,
      .high_frequency_mismatch_ratio = 0.01,
      .measurement_confidence = 0.25};

  const auto assessment = amt::separation::assess_reconstruction_comparison(metrics, 0.40);
  assert(assessment.confidence < 0.40);
  assert(assessment.overall_risk > 0.10);
}

}  // namespace

int main() {
  test_clean_reconstruction_scores_low_risk();
  test_damaged_reconstruction_scores_high_risk();
  test_low_confidence_cannot_look_certain();
  return 0;
}
