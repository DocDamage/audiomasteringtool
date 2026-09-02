#include "amt/translation/InstrumentSurvival.h"
#include <algorithm>
#include <cmath>

namespace amt::translation {

namespace {

double compute_rms(const amt::audio::AudioBuffer& buf) {
  if (buf.empty() || buf.channels() == 0) return 0.0;
  double sum = 0.0;
  std::size_t total = buf.frames() * buf.channels();
  for (std::size_t ch = 0; ch < buf.channels(); ++ch) {
    auto span = buf.channel(ch);
    for (std::size_t i = 0; i < buf.frames(); ++i) {
      sum += static_cast<double>(span[i]) * static_cast<double>(span[i]);
    }
  }
  return std::sqrt(sum / static_cast<double>(total));
}

}  // namespace

std::vector<ElementSurvivalMetrics> InstrumentSurvivalEvaluator::evaluate_survival(
    const amt::audio::AudioBuffer& full_mix,
    const amt::audio::AudioBuffer& small_speaker_mix,
    const amt::audio::AudioBuffer& mono_mix) {
  std::vector<ElementSurvivalMetrics> results;

  double full_rms = compute_rms(full_mix);
  double small_rms = compute_rms(small_speaker_mix);
  double mono_rms = compute_rms(mono_mix);

  double small_ratio = (full_rms > 1e-6) ? (small_rms / full_rms) : 1.0;
  double mono_ratio = (full_rms > 1e-6) ? (mono_rms / full_rms) : 1.0;

  // 1. Kick & Sub-Bass Survival
  {
    ElementSurvivalMetrics kick;
    kick.element_name = "Kick & Sub-Bass";
    // Small speakers filter sub-bass heavily; harmonic content determines survival
    kick.small_speaker_audibility = std::clamp(small_ratio * 1.2, 0.1, 1.0);
    kick.mono_survival_ratio = std::clamp(mono_ratio * 1.05, 0.2, 1.0);
    kick.has_audibility_risk = (kick.small_speaker_audibility < 0.45);
    if (kick.has_audibility_risk) {
      kick.recommendation = "Add upper-bass/low-mid harmonics (70-120 Hz) so kick and bass translate to phone/laptop speakers.";
    }
    results.push_back(kick);
  }

  // 2. Lead Vocal & Center Elements
  {
    ElementSurvivalMetrics vocal;
    vocal.element_name = "Lead Vocal / Mid Presence";
    vocal.small_speaker_audibility = std::clamp(small_ratio * 1.1, 0.4, 1.0);
    vocal.mono_survival_ratio = std::clamp(mono_ratio, 0.4, 1.0);
    vocal.has_audibility_risk = (vocal.mono_survival_ratio < 0.7);
    if (vocal.has_audibility_risk) {
      vocal.recommendation = "Check for stereo widening phase cancellation reducing vocal volume in mono.";
    }
    results.push_back(vocal);
  }

  // 3. Snare & Transient Punch
  {
    ElementSurvivalMetrics snare;
    snare.element_name = "Snare & Transients";
    snare.small_speaker_audibility = std::clamp(small_ratio * 1.15, 0.3, 1.0);
    snare.mono_survival_ratio = std::clamp(mono_ratio * 1.02, 0.3, 1.0);
    snare.has_audibility_risk = (snare.small_speaker_audibility < 0.5);
    if (snare.has_audibility_risk) {
      snare.recommendation = "Ensure snare body and 200 Hz transient survive bandpass filtering.";
    }
    results.push_back(snare);
  }

  return results;
}

}  // namespace amt::translation
