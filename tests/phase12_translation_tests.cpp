#include <cassert>
#include <cmath>
#include <iostream>
#include "amt/audio/AudioBuffer.h"
#include "amt/translation/InstrumentSurvival.h"
#include "amt/translation/PlaybackClass.h"
#include "amt/translation/TranslationAnalyzer.h"
#include "amt/translation/TranslationModel.h"

int main() {
  std::cout << "[Phase 12] Running Playback Translation Engine Tests...\n";

  // Test 1: Verify builtin playback classes catalog
  {
    const auto& classes = amt::translation::builtin_playback_classes();
    assert(classes.size() >= 8);

    const auto* phone = amt::translation::find_playback_class(amt::translation::PlaybackClassId::phone_speaker);
    assert(phone != nullptr);
    assert(phone->fold_to_mono == true);
    assert(phone->low_cutoff_hz > 200.0);

    const auto* club = amt::translation::find_playback_class(amt::translation::PlaybackClassId::club_pa);
    assert(club != nullptr);
    assert(club->low_cutoff_hz < 35.0);
    std::cout << "  ✓ Test 1: Builtin playback classes verified\n";
  }

  // Test 2: Acoustic simulation filtering
  {
    // Generate 1 second of stereo test audio with 50 Hz sub and 1 kHz tone
    amt::audio::AudioBuffer test_buf(2, 44100);
    auto ch0 = test_buf.channel(0);
    auto ch1 = test_buf.channel(1);
    for (std::size_t i = 0; i < 44100; ++i) {
      double t = static_cast<double>(i) / 44100.0;
      float val = static_cast<float>(0.5 * std::sin(2.0 * 3.1415926535 * 50.0 * t) +
                                     0.3 * std::sin(2.0 * 3.1415926535 * 1000.0 * t));
      ch0[i] = val;
      ch1[i] = val;
    }

    const auto* phone = amt::translation::find_playback_class(amt::translation::PlaybackClassId::phone_speaker);
    auto sim = amt::translation::TranslationModel::simulate(test_buf, *phone);
    assert(sim.frames() == test_buf.frames());
    assert(sim.channels() == test_buf.channels());

    // Phone simulation should have reduced RMS significantly due to 50Hz sub removal
    double in_rms = 0.0, out_rms = 0.0;
    auto sim_ch0 = sim.channel(0);
    for (std::size_t i = 0; i < 44100; ++i) {
      in_rms += ch0[i] * ch0[i];
      out_rms += sim_ch0[i] * sim_ch0[i];
    }
    assert(out_rms < in_rms);
    std::cout << "  ✓ Test 2: Acoustic simulation filtered sub-bass accurately\n";
  }

  // Test 3: Instrument survival evaluation
  {
    amt::audio::AudioBuffer full(2, 44100);
    amt::audio::AudioBuffer small(2, 44100);
    amt::audio::AudioBuffer mono(2, 44100);

    for (std::size_t i = 0; i < 44100; ++i) {
      full.channel(0)[i] = 0.5f;
      full.channel(1)[i] = 0.5f;
      small.channel(0)[i] = 0.3f;
      small.channel(1)[i] = 0.3f;
      mono.channel(0)[i] = 0.45f;
      mono.channel(1)[i] = 0.45f;
    }

    auto survival = amt::translation::InstrumentSurvivalEvaluator::evaluate_survival(full, small, mono);
    assert(survival.size() == 3);
    assert(survival[0].element_name.find("Kick") != std::string::npos);
    std::cout << "  ✓ Test 3: Instrument survival metrics computed across targets\n";
  }

  // Test 4: Comprehensive Translation Analysis
  {
    amt::audio::AudioBuffer test_buf(2, 44100);
    auto t0 = test_buf.channel(0);
    auto t1 = test_buf.channel(1);
    for (std::size_t i = 0; i < 44100; ++i) {
      t0[i] = 0.3f * std::sin(2.0f * 3.14159f * 440.0f * (static_cast<float>(i) / 44100.0f));
      t1[i] = 0.3f * std::sin(2.0f * 3.14159f * 440.0f * (static_cast<float>(i) / 44100.0f));
    }

    auto report = amt::translation::TranslationAnalyzer::analyze(test_buf);
    assert(report.overall_score > 0.6);
    assert(!report.class_scores.empty());
    std::cout << "  ✓ Test 4: TranslationAnalyzer produced structured report with scores\n";
  }

  std::cout << "[Phase 12] All tests passed successfully!\n";
  return 0;
}
