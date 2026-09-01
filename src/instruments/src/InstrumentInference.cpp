#include "amt/instruments/InstrumentInference.h"

#include <algorithm>
#include <cmath>

namespace amt::instruments {
InstrumentEvent resolve_instrument_scores(const std::vector<InstrumentScore>& scores,
                                          const double start_seconds, const double end_seconds,
                                          std::string model_id, std::string model_version) {
  InstrumentEvent event;
  event.start_seconds = std::max(0.0, start_seconds);
  event.end_seconds = std::max(event.start_seconds, end_seconds);
  event.model_id = std::move(model_id); event.model_version = std::move(model_version);
  const auto best = std::max_element(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
    return a.confidence < b.confidence;
  });
  if (best == scores.end() || !std::isfinite(best->confidence)) return event;
  const auto* node = find_taxonomy_node(best->taxonomy_id);
  double confidence = std::clamp(best->confidence, 0.0, 1.0);
  while (node != nullptr && confidence < node->minimum_confidence) node = find_taxonomy_node(node->parent_id);
  if (!node || node->id == "unknown") return event;
  event.taxonomy_id = node->id; event.display_label = node->display_label;
  event.family = node->family; event.source_role = node->source_role; event.confidence = confidence;
  event.evidence.push_back("confidence-calibrated hierarchical detector resolution");
  return event;
}
}  // namespace amt::instruments
