#include <cassert>
#include <string>

#include "amt/instruments/InstrumentEventTracker.h"
#include "amt/instruments/InstrumentInference.h"
#include "amt/instruments/InstrumentTaxonomy.h"
#include "amt/instruments/InstrumentSerialization.h"
#include "amt/instruments/InstrumentModelGovernance.h"

int main() {
  std::string error;
  assert(amt::instruments::validate_instrument_taxonomy(error));
  assert(amt::instruments::find_taxonomy_node("sub_808") != nullptr);

  auto exact = amt::instruments::resolve_instrument_scores({{"rhodes", 0.90}}, 1.0, 1.2);
  assert(exact.taxonomy_id == "rhodes");
  auto fallback = amt::instruments::resolve_instrument_scores({{"rhodes", 0.74}}, 1.0, 1.2);
  assert(fallback.taxonomy_id == "electric_piano");
  auto unknown = amt::instruments::resolve_instrument_scores({{"kick", 0.20}}, 1.0, 1.2);
  assert(unknown.taxonomy_id == "unknown");

  amt::instruments::InstrumentEventTracker tracker(0.2);
  tracker.push(exact);
  exact.start_seconds = 1.3; exact.end_seconds = 1.6; exact.confidence = 0.92;
  tracker.push(exact);
  const auto events = tracker.finalize();
  assert(events.size() == 1U && events.front().end_seconds == 1.6);
  const auto json = amt::instruments::instrument_events_to_json(events);
  assert(json.find("\"taxonomy_id\":\"rhodes\"") != std::string::npos);
  const auto blocked = amt::instruments::evaluate_instrument_model_for_production({});
  assert(!blocked.production_eligible && !blocked.blockers.empty());
  return 0;
}
