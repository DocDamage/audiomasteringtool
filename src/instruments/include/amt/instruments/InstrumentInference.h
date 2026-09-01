#pragma once

#include <string>
#include <vector>

#include "amt/instruments/InstrumentEvent.h"

namespace amt::instruments {

struct InstrumentScore { std::string taxonomy_id; double confidence{0.0}; };

// Resolves a detector score to the most specific taxonomy label allowed by its
// per-node confidence policy. Weak evidence walks upward; no valid ancestor is
// represented explicitly as unknown rather than forcing a winner.
[[nodiscard]] InstrumentEvent resolve_instrument_scores(
    const std::vector<InstrumentScore>& scores, double start_seconds,
    double end_seconds, std::string model_id = {}, std::string model_version = {});

}  // namespace amt::instruments
