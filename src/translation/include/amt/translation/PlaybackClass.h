#pragma once

#include <string>
#include <vector>

namespace amt::translation {

enum class PlaybackClassId {
  studio_monitors,
  headphones,
  earbuds,
  phone_speaker,
  laptop_speaker,
  bluetooth_speaker,
  car_audio,
  mono_system,
  club_pa
};

struct PlaybackClassInfo {
  PlaybackClassId id{PlaybackClassId::studio_monitors};
  std::string key;
  std::string name;
  std::string description;
  double low_cutoff_hz{20.0};
  double high_cutoff_hz{20000.0};
  double mid_emphasis_freq_hz{0.0};
  double mid_emphasis_gain_db{0.0};
  bool fold_to_mono{false};
  double max_linear_peak_db{-0.5};
};

[[nodiscard]] const std::vector<PlaybackClassInfo>& builtin_playback_classes();
[[nodiscard]] const PlaybackClassInfo* find_playback_class(PlaybackClassId id);
[[nodiscard]] const PlaybackClassInfo* find_playback_class(const std::string& key);

}  // namespace amt::translation
