#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/repair/Declipping.h"
#include "amt/repair/Denoise.h"
#include "amt/repair/RepairPolicy.h"
#include "amt/repair/RepairValidator.h"
#include "amt/repair/TransientRepair.h"

namespace {

amt::audio::AudioBuffer make_clipped_audio(int sample_rate, double duration_sec) {
  const std::size_t frames = static_cast<std::size_t>(duration_sec * sample_rate);
  amt::audio::AudioBuffer buf(2, frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const double raw = 1.3 * std::sin(2.0 * 3.1415926535 * 200.0 * static_cast<double>(i) / static_cast<double>(sample_rate));
    const float val = static_cast<float>(std::clamp(raw, -0.99, 0.99));
    buf.channel(0).data()[i] = val;
    buf.channel(1).data()[i] = val;
  }
  return buf;
}

void test_declipping_and_validator() {
  const int sr = 44100;
  auto audio = make_clipped_audio(sr, 0.2);
  const auto original_copy = audio;

  amt::repair::DeclippingSettings settings{};
  settings.threshold_linear = 0.98;
  const auto report = amt::repair::process_declipping(audio, settings);

  assert(report.applied);
  assert(report.clipped_samples_reconstructed > 0);

  amt::repair::RepairValidator validator;
  const auto validation = validator.validate(original_copy, audio);
  assert(validation.passed);
}

void test_transient_repair_and_denoise() {
  const int sr = 44100;
  amt::audio::AudioBuffer buf(2, static_cast<std::size_t>(sr));
  for (std::size_t i = 0; i < static_cast<std::size_t>(sr); ++i) {
    buf.channel(0).data()[i] = (i % 4000 == 0) ? 0.9f : 0.001f;
    buf.channel(1).data()[i] = (i % 4000 == 0) ? 0.9f : 0.001f;
  }

  amt::repair::TransientRepairSettings tr_settings{};
  const auto tr_report = amt::repair::process_transient_repair(buf, tr_settings, sr);
  assert(tr_report.applied);
  assert(tr_report.transients_enhanced > 0);

  amt::repair::DenoiseSettings dn_settings{};
  const auto dn_report = amt::repair::process_denoise(buf, dn_settings, sr);
  assert(dn_report.applied);
}

}  // namespace

int main() {
  test_declipping_and_validator();
  test_transient_repair_and_denoise();
  return 0;
}
