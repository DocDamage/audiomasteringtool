#pragma once

#include <string>
#include <vector>
#include "amt/translation/InstrumentSurvival.h"
#include "amt/translation/PlaybackClass.h"

namespace amt::translation {

struct ClassScore {
  PlaybackClassId class_id{PlaybackClassId::studio_monitors};
  std::string name;
  double score{1.0}; // [0.0, 1.0]
  double headroom_db{0.0};
  std::string notes;
};

struct TranslationReport {
  double overall_score{0.85}; // [0.0, 1.0]
  double small_speaker_score{0.80};
  double mono_compatibility_score{0.90};
  double car_club_score{0.88};
  std::vector<ClassScore> class_scores;
  std::vector<ElementSurvivalMetrics> instrument_survival;
  std::vector<std::string> warnings;
};

class TranslationScorer {
 public:
  [[nodiscard]] static TranslationReport score_translation(
      const std::vector<ClassScore>& class_scores,
      const std::vector<ElementSurvivalMetrics>& instrument_survival);
};

}  // namespace amt::translation
