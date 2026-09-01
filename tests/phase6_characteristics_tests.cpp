#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/instruments/InstrumentCharacteristics.h"
#include "amt/instruments/SeparationAssistedDetector.h"

namespace {

amt::audio::AudioBuffer make_sine_wave(double freq_hz, int sample_rate, double duration_sec) {
  const std::size_t frames = static_cast<std::size_t>(duration_sec * sample_rate);
  amt::audio::AudioBuffer buf(2, frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const float val = static_cast<float>(0.5 * std::sin(2.0 * 3.1415926535 * freq_hz * static_cast<double>(i) / static_cast<double>(sample_rate)));
    buf.channel(0).data()[i] = val;
    buf.channel(1).data()[i] = val;
  }
  return buf;
}

void test_f0_and_centroid_estimation() {
  const int sr = 44100;
  const auto sine_100hz = make_sine_wave(100.0, sr, 0.5);
  const auto charact = amt::instruments::extract_characteristics(sine_100hz, sr);

  assert(charact.f0_hz > 90.0 && charact.f0_hz < 110.0);
  assert(charact.spectral_centroid_hz > 80.0 && charact.spectral_centroid_hz < 150.0);
  assert(charact.stereo_correlation > 0.98);
}

void test_separation_assisted_detector() {
  const int sr = 44100;
  const auto mix = make_sine_wave(100.0, sr, 1.0);

  amt::instruments::SeparationStems stems{};
  stems.available = true;
  stems.drums = make_sine_wave(55.0, sr, 1.0);
  stems.bass = make_sine_wave(45.0, sr, 1.0);
  stems.vocals = make_sine_wave(440.0, sr, 1.0);
  stems.other = amt::audio::AudioBuffer(2, sr);

  amt::instruments::SeparationAssistedDetector detector;
  const auto events = detector.detect_instruments(mix, &stems, sr);

  assert(!events.empty());
  bool found_kick = false;
  bool found_bass = false;
  bool found_vocal = false;

  for (const auto& ev : events) {
    if (ev.source_role == amt::instruments::SourceRole::drums) found_kick = true;
    if (ev.source_role == amt::instruments::SourceRole::bass) found_bass = true;
    if (ev.source_role == amt::instruments::SourceRole::vocals) found_vocal = true;
  }

  assert(found_kick);
  assert(found_bass);
  assert(found_vocal);
}

}  // namespace

int main() {
  test_f0_and_centroid_estimation();
  test_separation_assisted_detector();
  return 0;
}
