#include "amt/instruments/InstrumentEventTracker.h"

#include <algorithm>

namespace amt::instruments {
InstrumentEventTracker::InstrumentEventTracker(const double maximum_gap_seconds)
    : maximum_gap_seconds_(std::max(0.0, maximum_gap_seconds)) {}
void InstrumentEventTracker::push(InstrumentEvent event) {
  if (event.end_seconds < event.start_seconds) return;
  if (!events_.empty()) {
    auto& previous = events_.back();
    if (previous.taxonomy_id == event.taxonomy_id && previous.taxonomy_id != "unknown" &&
        event.start_seconds <= previous.end_seconds + maximum_gap_seconds_) {
      previous.end_seconds = std::max(previous.end_seconds, event.end_seconds);
      previous.confidence = std::max(previous.confidence, event.confidence);
      return;
    }
  }
  events_.push_back(std::move(event));
}
std::vector<InstrumentEvent> InstrumentEventTracker::finalize() { return std::move(events_); }
}  // namespace amt::instruments
