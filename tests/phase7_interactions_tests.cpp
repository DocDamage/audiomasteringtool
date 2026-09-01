#include <cassert>
#include "amt/interactions/InteractionEngine.h"
int main() {
  const amt::interactions::SourceActivity kick{"kick", .90, 3.0, .9, 58.0, .95, .05, .95, 10.0, 8.0};
  const amt::interactions::SourceActivity bass{"sub_808", .88, .2, .85, 52.0, .40, .1, .94, 10.0, 8.0};
  const auto evidence = amt::interactions::analyze_interaction(kick, bass);
  assert(evidence.confidence > .7 && evidence.low_band_masking > .4);
  assert(!amt::interactions::plan_bounded_repairs(evidence).empty());
  assert(amt::interactions::validate_repair_damage({}).safe);
  assert(!amt::interactions::validate_repair_damage({.transient_loss=.2}).safe);
}
