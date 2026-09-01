#pragma once

#include <string>
#include <vector>

#include "amt/instruments/InstrumentEvent.h"

namespace amt::instruments {

[[nodiscard]] std::string instrument_events_to_json(const std::vector<InstrumentEvent>& events);

}  // namespace amt::instruments
