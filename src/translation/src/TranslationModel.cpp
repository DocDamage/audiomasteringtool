#include "amt/translation/TranslationModel.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace amt::translation {

namespace {

const std::vector<PlaybackClassInfo> kBuiltinClasses = {
    {PlaybackClassId::studio_monitors, "studio_monitors", "Studio Monitors",
     "Full-range flat reference playback", 20.0, 20000.0, 0.0, 0.0, false, -0.1},
    {PlaybackClassId::headphones, "headphones", "Reference Headphones",
     "Extended low and high frequency response with wide stereo imaging", 15.0, 22000.0, 0.0, 0.0, false, -0.2},
    {PlaybackClassId::earbuds, "earbuds", "Consumer Earbuds",
     "Bright high-mid presentation with sub-bass rolloff", 45.0, 17000.0, 3200.0, 2.5, false, -0.5},
    {PlaybackClassId::phone_speaker, "phone_speaker", "Phone Speaker",
     "Severe low-end cutoff below 250 Hz, resonant upper-mids, mono playback", 260.0, 14000.0, 2500.0, 4.0, true, -1.0},
    {PlaybackClassId::laptop_speaker, "laptop_speaker", "Laptop Speaker",
     "Bandwidth limited with no sub-bass and shallow stereo width", 180.0, 15000.0, 1800.0, 2.0, false, -0.8},
    {PlaybackClassId::bluetooth_speaker, "bluetooth_speaker", "Small Bluetooth Speaker",
     "Mono-summed small portable enclosure with 70 Hz high-pass", 75.0, 16000.0, 120.0, 2.0, true, -0.5},
    {PlaybackClassId::car_audio, "car_audio", "Car Audio System",
     "Acoustic cabin mode buildup around 110 Hz with high-frequency absorption", 30.0, 16500.0, 110.0, 3.5, false, -0.3},
    {PlaybackClassId::mono_system, "mono_system", "Mono Club/Radio System",
     "Strict mono sum evaluating phase cancellation and center masking", 25.0, 19000.0, 0.0, 0.0, true, -0.1},
    {PlaybackClassId::club_pa, "club_pa", "Club PA / Sub Array",
     "Heavy sub-bass emphasis (< 80 Hz) and powerful dynamic transient demands", 28.0, 18000.0, 55.0, 4.0, false, -0.1}};

// Simple 1st-order IIR high-pass / low-pass state filter
struct SimpleFilter {
  double a0{1.0}, b1{0.0};
  double z1{0.0};

  void set_high_pass(double freq_hz, double sample_rate) {
    double rc = 1.0 / (2.0 * std::numbers::pi * freq_hz);
    double dt = 1.0 / sample_rate;
    double alpha = rc / (rc + dt);
    a0 = alpha;
    b1 = alpha;
    z1 = 0.0;
  }

  void set_low_pass(double freq_hz, double sample_rate) {
    double rc = 1.0 / (2.0 * std::numbers::pi * freq_hz);
    double dt = 1.0 / sample_rate;
    double alpha = dt / (rc + dt);
    a0 = alpha;
    b1 = 1.0 - alpha;
    z1 = 0.0;
  }

  double process_hp(double in) {
    double out = a0 * (in - z1);
    z1 = in;
    return out;
  }

  double process_lp(double in) {
    double out = a0 * in + b1 * z1;
    z1 = out;
    return out;
  }
};

}  // namespace

const std::vector<PlaybackClassInfo>& builtin_playback_classes() {
  return kBuiltinClasses;
}

const PlaybackClassInfo* find_playback_class(PlaybackClassId id) {
  for (const auto& item : kBuiltinClasses) {
    if (item.id == id) return &item;
  }
  return nullptr;
}

const PlaybackClassInfo* find_playback_class(const std::string& key) {
  for (const auto& item : kBuiltinClasses) {
    if (item.key == key) return &item;
  }
  return nullptr;
}

amt::audio::AudioBuffer TranslationModel::simulate(
    const amt::audio::AudioBuffer& input,
    const PlaybackClassInfo& target_class) {
  amt::audio::AudioBuffer output(input.channels(), input.frames());

  double sr = 44100.0;

  for (std::size_t ch = 0; ch < input.channels(); ++ch) {
    SimpleFilter hp, lp;
    hp.set_high_pass(target_class.low_cutoff_hz, sr);
    lp.set_low_pass(target_class.high_cutoff_hz, sr);

    auto in_span = input.channel(ch);
    auto out_span = output.channel(ch);

    for (std::size_t i = 0; i < input.frames(); ++i) {
      double val = static_cast<double>(in_span[i]);
      val = hp.process_hp(val);
      val = lp.process_lp(val);

      // Mid emphasis / cabin boost if specified
      if (target_class.mid_emphasis_gain_db != 0.0) {
        val *= std::pow(10.0, (target_class.mid_emphasis_gain_db * 0.25) / 20.0);
      }

      // Soft limiting to simulate speaker driver compression
      double peak_limit = std::pow(10.0, target_class.max_linear_peak_db / 20.0);
      if (val > peak_limit) {
        val = peak_limit + (val - peak_limit) * 0.2;
      } else if (val < -peak_limit) {
        val = -peak_limit + (val + peak_limit) * 0.2;
      }

      out_span[i] = static_cast<float>(val);
    }
  }

  // Handle mono folding
  if (target_class.fold_to_mono && output.channels() >= 2) {
    auto ch0 = output.channel(0);
    auto ch1 = output.channel(1);
    for (std::size_t i = 0; i < output.frames(); ++i) {
      float mono = 0.5f * (ch0[i] + ch1[i]);
      ch0[i] = mono;
      ch1[i] = mono;
    }
  }

  return output;
}

}  // namespace amt::translation
