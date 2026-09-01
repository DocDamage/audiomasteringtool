#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/interactions/BassTracker.h"
#include "amt/interactions/InteractionEngine.h"
#include "amt/interactions/KickTracker.h"
#include "amt/interactions/MaskingAnalyzer.h"

namespace {

amt::audio::AudioBuffer make_kick_and_bass_audio(int sample_rate, double duration_sec) {
  const std::size_t frames = static_cast<std::size_t>(duration_sec * sample_rate);
  amt::audio::AudioBuffer buf(2, frames);

  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(sample_rate);
    // 4 on the floor kick bursts
    double kick_val = 0.0;
    const double beat_pos = std::fmod(t, 0.5);
    if (beat_pos < 0.1) {
      const double env = std::exp(-beat_pos * 30.0);
      kick_val = env * std::sin(2.0 * 3.1415926535 * (55.0 + 80.0 * env) * beat_pos);
    }

    // Sustained 808 sub bass @ 45 Hz
    const double bass_val = 0.4 * std::sin(2.0 * 3.1415926535 * 45.0 * t);

    const float total = static_cast<float>(kick_val * 0.6 + bass_val * 0.4);
    buf.channel(0).data()[i] = total;
    buf.channel(1).data()[i] = total;
  }
  return buf;
}

void test_kick_and_bass_tracking() {
  const int sr = 44100;
  const auto audio = make_kick_and_bass_audio(sr, 2.0);

  amt::interactions::KickTracker kick_tracker;
  const auto kick_res = kick_tracker.track_kicks(audio, nullptr, 0, sr);
  assert(!kick_res.kicks.empty());
  assert(kick_res.average_fundamental_hz > 40.0 && kick_res.average_fundamental_hz < 80.0);

  amt::interactions::BassTracker bass_tracker;
  const auto bass_res = bass_tracker.track_bass(audio, nullptr, 0, sr);
  assert(bass_res.has_808_character);
  assert(bass_res.sub_mono_correlation > 0.9);

  amt::interactions::MaskingAnalyzer masking_analyzer;
  const auto evidence = masking_analyzer.evaluate_kick_bass_interaction(kick_res, bass_res, audio);
  assert(evidence.confidence > 0.7);
  assert(evidence.spectral_overlap > 0.5);

  const auto repairs = amt::interactions::plan_bounded_repairs(evidence);
  assert(!repairs.empty());
}

}  // namespace

int main() {
  test_kick_and_bass_tracking();
  return 0;
}
