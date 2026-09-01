#pragma once

#include <vector>

#include "amt/instruments/InstrumentEvent.h"

namespace amt::instruments {

class InstrumentEventTracker {
 public:
  explicit InstrumentEventTracker(double maximum_gap_seconds = 0.20);
  void push(InstrumentEvent event);
  [[nodiscard]] std::vector<InstrumentEvent> finalize();

 private:
  double maximum_gap_seconds_;
  std::vector<InstrumentEvent> events_;
};

}  // namespace amt::instruments
