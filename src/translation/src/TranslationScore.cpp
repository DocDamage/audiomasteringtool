#include "amt/translation/TranslationScore.h"
#include <algorithm>
#include <numeric>

namespace amt::translation {

TranslationReport TranslationScorer::score_translation(
    const std::vector<ClassScore>& class_scores,
    const std::vector<ElementSurvivalMetrics>& instrument_survival) {
  TranslationReport report;
  report.class_scores = class_scores;
  report.instrument_survival = instrument_survival;

  double total_score = 0.0;
  int count = 0;
  double small_sum = 0.0;
  int small_count = 0;

  for (const auto& cs : class_scores) {
    total_score += cs.score;
    count++;

    if (cs.class_id == PlaybackClassId::phone_speaker ||
        cs.class_id == PlaybackClassId::laptop_speaker ||
        cs.class_id == PlaybackClassId::bluetooth_speaker) {
      small_sum += cs.score;
      small_count++;
    } else if (cs.class_id == PlaybackClassId::mono_system) {
      report.mono_compatibility_score = cs.score;
    } else if (cs.class_id == PlaybackClassId::car_audio ||
               cs.class_id == PlaybackClassId::club_pa) {
      report.car_club_score = cs.score;
    }
  }

  report.overall_score = (count > 0) ? (total_score / static_cast<double>(count)) : 0.85;
  if (small_count > 0) {
    report.small_speaker_score = small_sum / static_cast<double>(small_count);
  }

  // Populate warnings from instrument survival analysis
  for (const auto& elem : instrument_survival) {
    if (elem.has_audibility_risk && !elem.recommendation.empty()) {
      report.warnings.push_back(elem.element_name + ": " + elem.recommendation);
    }
  }

  return report;
}

}  // namespace amt::translation
