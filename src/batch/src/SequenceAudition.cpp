#include "amt/batch/SequenceAudition.h"
#include <cmath>

namespace amt::batch {

std::vector<TransitionInfo> SequenceAudition::analyze_transitions(
    const BatchAlbumProject& album) {
  std::vector<TransitionInfo> transitions;
  if (album.tracks.size() < 2) return transitions;

  for (std::size_t i = 0; i + 1 < album.tracks.size(); ++i) {
    const auto& t1 = album.tracks[i];
    const auto& t2 = album.tracks[i + 1];

    TransitionInfo info;
    info.from_track_idx = i;
    info.to_track_idx = i + 1;
    info.gap_seconds = 2.0;

    double l1 = (t1.master_lufs > -70.0) ? t1.master_lufs : t1.input_lufs;
    double l2 = (t2.master_lufs > -70.0) ? t2.master_lufs : t2.input_lufs;

    info.loudness_jump_lu = l2 - l1;
    info.has_loudness_jump_warning = (std::abs(info.loudness_jump_lu) > 3.5);

    transitions.push_back(info);
  }

  return transitions;
}

}  // namespace amt::batch
