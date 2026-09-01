#include "amt/interactions/MaskingAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace amt::interactions {

InteractionEvidence MaskingAnalyzer::evaluate_kick_bass_interaction(
    const KickAnalysis& kick,
    const BassAnalysis& bass,
    const amt::audio::AudioBuffer& full_mix) const {
  InteractionEvidence ev{};
  ev.first_id = "percussion.drums.kick";
  ev.second_id = bass.has_808_character ? "bass.synth.808" : "bass.electric";

  if (full_mix.frames() == 0 || (kick.kicks.empty() && bass.note_frequencies_hz.empty())) {
    return ev;
  }

  ev.confidence = 0.85;
  ev.temporal_overlap = kick.kicks.empty() ? 0.0 : 0.75;

  // Frequency collision delta
  const double freq_diff = std::abs(kick.average_fundamental_hz - bass.average_f0_hz);
  if (freq_diff < 20.0) {
    ev.spectral_overlap = 0.85;
    ev.low_band_masking = 0.70;
    ev.onset_masking = 0.65;
    ev.evidence.push_back("low_end_frequency_collision");
  } else {
    ev.spectral_overlap = 0.35;
    ev.low_band_masking = 0.25;
    ev.onset_masking = 0.20;
  }

  // Phase & stereo sub check
  if (bass.sub_mono_correlation < 0.8) {
    ev.phase_risk = std::clamp((0.8 - bass.sub_mono_correlation) * 2.0, 0.0, 1.0);
    ev.mono_survival_risk = ev.phase_risk;
    ev.evidence.push_back("stereo_sub_phase_cancellation");
  }

  ev.limiter_contribution = (ev.low_band_masking > 0.5) ? 0.60 : 0.20;
  return ev;
}

}  // namespace amt::interactions
