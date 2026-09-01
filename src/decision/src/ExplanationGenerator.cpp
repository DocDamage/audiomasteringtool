#include "amt/decision/ExplanationGenerator.h"

#include <iomanip>
#include <sstream>

namespace amt::decision {

DecisionExplanation ExplanationGenerator::generate_explanation(
    const TrackDiagnosis& diagnosis,
    const RankedFinalists& finalists,
    const DecisionEvidence& evidence) const {
  DecisionExplanation expl{};

  std::ostringstream summary;
  summary << "Audio Analysis Summary:\n"
          << "- Integrated Loudness: " << std::fixed << std::setprecision(1) << evidence.integrated_lufs << " LUFS\n"
          << "- True Peak: " << evidence.true_peak_dbtp << " dBTP\n"
          << "- Crest Factor: " << evidence.crest_factor_db << " dB\n"
          << "- Character: " << diagnosis.primary_genre_tendency << "\n";

  if (!diagnosis.issues.empty()) {
    summary << "\nIdentified Opportunities:\n";
    for (const auto& issue : diagnosis.issues) {
      summary << " * " << issue << "\n";
    }
  }

  expl.summary_text = summary.str();

  std::ostringstream ma;
  ma << "Master A (Recommended: " << finalists.master_a_recommended.display_name << "):\n"
     << finalists.master_a_recommended.philosophy << "\nKey adjustments:\n";
  for (const auto& r : finalists.master_a_recommended.branch_settings.rationale) {
    ma << " * " << r << "\n";
  }
  expl.master_a_explanation = ma.str();

  std::ostringstream mb;
  mb << "Master B (Alternative: " << finalists.master_b_alternative.display_name << "):\n"
     << finalists.master_b_alternative.philosophy << "\nKey adjustments:\n";
  for (const auto& r : finalists.master_b_alternative.branch_settings.rationale) {
    mb << " * " << r << "\n";
  }
  expl.master_b_explanation = mb.str();

  std::ostringstream json;
  json << "{\n"
       << "  \"schema_version\": 2,\n"
       << "  \"source_lufs\": " << evidence.integrated_lufs << ",\n"
       << "  \"source_true_peak\": " << evidence.true_peak_dbtp << ",\n"
       << "  \"master_a_profile\": \"" << finalists.master_a_recommended.profile_id << "\",\n"
       << "  \"master_b_profile\": \"" << finalists.master_b_alternative.profile_id << "\",\n"
       << "  \"master_a_score\": " << finalists.master_a_score.overall_score << ",\n"
       << "  \"master_b_score\": " << finalists.master_b_score.overall_score << "\n"
       << "}";
  expl.json_report = json.str();

  return expl;
}

}  // namespace amt::decision
