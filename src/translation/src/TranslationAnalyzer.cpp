#include "amt/translation/TranslationAnalyzer.h"
#include <algorithm>
#include <cmath>
#include "amt/translation/InstrumentSurvival.h"
#include "amt/translation/PlaybackClass.h"
#include "amt/translation/TranslationModel.h"

namespace amt::translation {

TranslationReport TranslationAnalyzer::analyze(
    const amt::audio::AudioBuffer& buffer) {
  if (buffer.empty()) {
    return TranslationReport{};
  }

  const auto& classes = builtin_playback_classes();
  std::vector<ClassScore> class_scores;
  class_scores.reserve(classes.size());

  amt::audio::AudioBuffer phone_sim;
  amt::audio::AudioBuffer mono_sim;

  for (const auto& cls : classes) {
    auto sim = TranslationModel::simulate(buffer, cls);

    if (cls.id == PlaybackClassId::phone_speaker) {
      phone_sim = sim;
    } else if (cls.id == PlaybackClassId::mono_system) {
      mono_sim = sim;
    }

    // Compute peak / rms for this simulation
    double peak = 0.0;
    double sum_sq = 0.0;
    std::size_t total_samples = sim.frames() * sim.channels();

    for (std::size_t ch = 0; ch < sim.channels(); ++ch) {
      auto span = sim.channel(ch);
      for (std::size_t i = 0; i < sim.frames(); ++i) {
        double v = std::abs(static_cast<double>(span[i]));
        if (v > peak) peak = v;
        sum_sq += v * v;
      }
    }

    double rms = (total_samples > 0) ? std::sqrt(sum_sq / static_cast<double>(total_samples)) : 0.0;
    double crest_factor = (rms > 1e-6) ? (peak / rms) : 1.0;

    // Score based on reasonable crest retention and no excessive clipping
    double score = 1.0;
    if (crest_factor < 2.0) score -= 0.15; // overly smashed
    if (peak > 1.2) score -= 0.20;         // distortion overload

    ClassScore cs;
    cs.class_id = cls.id;
    cs.name = cls.name;
    cs.score = std::clamp(score, 0.4, 1.0);
    cs.headroom_db = (peak > 0.0) ? (20.0 * std::log10(peak)) : -80.0;
    cs.notes = (cs.score > 0.85) ? "Good translation" : "Potential playback strain";
    class_scores.push_back(cs);
  }

  if (phone_sim.empty()) {
    phone_sim = buffer;
  }
  if (mono_sim.empty()) {
    mono_sim = buffer;
  }

  auto survival = InstrumentSurvivalEvaluator::evaluate_survival(buffer, phone_sim, mono_sim);
  return TranslationScorer::score_translation(class_scores, survival);
}

}  // namespace amt::translation
