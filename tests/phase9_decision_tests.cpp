#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>

#include "amt/decision/CandidateGenerator.h"
#include "amt/decision/CandidateRanker.h"
#include "amt/decision/CandidateScorer.h"
#include "amt/decision/Diagnosis.h"
#include "amt/decision/Evidence.h"
#include "amt/decision/ExplanationGenerator.h"

namespace {

void test_decision_workflow() {
  amt::decision::DecisionEvidence ev{};
  ev.integrated_lufs = -18.0;
  ev.true_peak_dbtp = -3.5;
  ev.loudness_range_lu = 8.0;
  ev.crest_factor_db = 14.0;
  ev.spectral_centroid_hz = 3200.0;
  ev.stereo_correlation = 0.92;
  ev.has_kick_bass_masking = true;
  ev.detected_instruments = {"percussion.drums.kick", "bass.synth.808", "vocal.lead"};

  const auto diag = amt::decision::diagnose_track(ev);
  assert(!diag.issues.empty());

  amt::decision::CandidateGenerator generator;
  amt::decision::MasteringConstraints constraints{};
  constraints.target_lufs = -14.0;
  constraints.target_true_peak_dbtp = -1.0;

  const auto candidates = generator.generate_candidates(ev, constraints);
  assert(candidates.size() >= 2);

  amt::decision::CandidateRanker ranker;
  const auto finalists = ranker.select_finalists(candidates, ev, constraints);

  assert(!finalists.master_a_recommended.profile_id.empty());
  assert(!finalists.master_b_alternative.profile_id.empty());
  assert(finalists.master_a_score.overall_score >= finalists.master_b_score.overall_score);

  amt::decision::ExplanationGenerator expl_gen;
  const auto expl = expl_gen.generate_explanation(diag, finalists, ev);
  assert(!expl.summary_text.empty());
  assert(!expl.master_a_explanation.empty());
  assert(!expl.json_report.empty());
}

}  // namespace

int main() {
  test_decision_workflow();
  return 0;
}
