#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <filesystem>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/reference/ReferenceAnalysis.h"
#include "amt/reference/ReferenceInfluence.h"
#include "amt/reference/ReferenceProfile.h"
#include "amt/reference/ReferenceStore.h"

namespace {

amt::audio::AudioBuffer make_test_tone(double freq_hz, int sample_rate, double duration_sec) {
  const std::size_t frames = static_cast<std::size_t>(duration_sec * sample_rate);
  amt::audio::AudioBuffer buf(2, frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const float val = static_cast<float>(0.5 * std::sin(2.0 * 3.1415926535 * freq_hz * static_cast<double>(i) / static_cast<double>(sample_rate)));
    buf.channel(0).data()[i] = val;
    buf.channel(1).data()[i] = val;
  }
  return buf;
}

void test_reference_analysis_and_influence() {
  const int sr = 44100;
  const auto ref_audio = make_test_tone(100.0, sr, 0.5);
  const auto src_audio = make_test_tone(1000.0, sr, 0.5);

  amt::reference::ReferenceAnalyzer analyzer;
  const auto ref_profile = analyzer.extract_profile(ref_audio, "Reference Track A", sr);
  const auto src_profile = analyzer.extract_profile(src_audio, "Source Mix", sr);

  assert(!ref_profile.profile_id.empty());
  assert(ref_profile.integrated_lufs > -60.0);

  amt::reference::ReferenceInfluenceEngine engine;
  const auto plan_loose = engine.compute_influence(ref_profile, src_profile, amt::reference::InfluenceStrength::loose);
  const auto plan_medium = engine.compute_influence(ref_profile, src_profile, amt::reference::InfluenceStrength::medium);
  const auto plan_close = engine.compute_influence(ref_profile, src_profile, amt::reference::InfluenceStrength::close);

  assert(!plan_medium.adjustments_summary.empty());
  assert(std::abs(plan_close.recommended_eq.low_shelf_gain_db) >= std::abs(plan_loose.recommended_eq.low_shelf_gain_db));
}

void test_reference_store_and_my_sound() {
  const auto root = std::filesystem::temp_directory_path() / "amt-phase10-tests";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  amt::reference::ReferenceStore store(root);

  amt::reference::ReferenceProfile prof1{};
  prof1.profile_id = "prof_1";
  prof1.display_name = "Ref 1";
  prof1.integrated_lufs = -14.0;
  prof1.true_peak_dbtp = -1.0;
  prof1.spectrum.bass_db = -18.0;

  amt::reference::ReferenceProfile prof2{};
  prof2.profile_id = "prof_2";
  prof2.display_name = "Ref 2";
  prof2.integrated_lufs = -12.0;
  prof2.true_peak_dbtp = -0.5;
  prof2.spectrum.bass_db = -16.0;

  std::string err;
  assert(store.save_profile(prof1, err));
  assert(store.save_profile(prof2, err));

  const auto loaded = store.load_profile(root / "prof_1.json", err);
  assert(loaded.has_value());
  assert(loaded->profile_id == "prof_1");

  const auto my_sound = store.aggregate_profiles({prof1, prof2}, "My Custom Sound");
  assert(my_sound.reference_count == 2);
  assert(my_sound.aggregate_profile.integrated_lufs == -13.0);
  assert(my_sound.aggregate_profile.true_peak_dbtp == -0.75);

  assert(store.save_my_sound(my_sound, err));
  const auto loaded_my_sound = store.load_my_sound(err);
  assert(loaded_my_sound.has_value());
}

}  // namespace

int main() {
  test_reference_analysis_and_influence();
  test_reference_store_and_my_sound();
  return 0;
}
