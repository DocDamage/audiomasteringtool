#include "amt/decision/CandidateGenerator.h"

namespace amt::decision {

std::vector<MasterCandidateProfile> CandidateGenerator::generate_candidates(
    const DecisionEvidence& evidence,
    const MasteringConstraints& constraints) const {
  std::vector<MasterCandidateProfile> candidates;

  (void)evidence;

  // Candidate 1: Balanced Modern Polish (Master A candidate)
  MasterCandidateProfile cand_a{};
  cand_a.profile_id = "candidate_balanced";
  cand_a.display_name = "Modern Balanced";
  cand_a.philosophy = "Targeted commercial clarity with controlled low-end weight and competitive punch.";
  cand_a.branch_settings.id = "master_a";
  cand_a.branch_settings.name = "Master A";
  cand_a.branch_settings.recommended = true;
  cand_a.branch_settings.target_lufs = constraints.target_lufs;
  cand_a.branch_settings.ceiling_dbtp = constraints.target_true_peak_dbtp;
  cand_a.branch_settings.preservation_bias = 0.35;
  cand_a.branch_settings.rationale = {
      "High shelf polish (+1.2 dB @ 10kHz) for modern openness.",
      "Controlled low-end foundation (+0.8 dB @ 80Hz).",
      "True-peak ceiling at " + std::to_string(constraints.target_true_peak_dbtp) + " dBTP."
  };
  candidates.push_back(cand_a);

  // Candidate 2: Dynamic Preservation (Master B candidate)
  MasterCandidateProfile cand_b{};
  cand_b.profile_id = "candidate_preservation";
  cand_b.display_name = "Organic Dynamic Preservation";
  cand_b.philosophy = "Minimal coloration, preserving original mix transients, micro-dynamics, and natural space.";
  cand_b.branch_settings.id = "master_b";
  cand_b.branch_settings.name = "Master B";
  cand_b.branch_settings.recommended = false;
  cand_b.branch_settings.target_lufs = constraints.target_lufs - 1.5;
  cand_b.branch_settings.ceiling_dbtp = constraints.target_true_peak_dbtp;
  cand_b.branch_settings.preservation_bias = 0.85;
  cand_b.branch_settings.rationale = {
      "Transparent gain and gentle limiter ceiling preservation.",
      "Original transient punch and micro-dynamics retained.",
      "Zero aggressive tonal shifts."
  };
  candidates.push_back(cand_b);

  // Candidate 3: Warm Analog Character (Alternative exploration)
  MasterCandidateProfile cand_c{};
  cand_c.profile_id = "candidate_warm";
  cand_c.display_name = "Warm Character";
  cand_c.philosophy = "Thick, rounded low-mid warmth with smooth high-frequency roll-off.";
  cand_c.branch_settings.id = "master_warm";
  cand_c.branch_settings.name = "Master Alternative";
  cand_c.branch_settings.recommended = false;
  cand_c.branch_settings.target_lufs = constraints.target_lufs - 0.5;
  cand_c.branch_settings.ceiling_dbtp = constraints.target_true_peak_dbtp;
  cand_c.branch_settings.preservation_bias = 0.60;
  cand_c.branch_settings.rationale = {
      "Rounded low-end boost (+1.5 dB @ 100Hz).",
      "De-harshing high frequency softening (-0.5 dB @ 8kHz)."
  };
  candidates.push_back(cand_c);

  return candidates;
}

}  // namespace amt::decision
