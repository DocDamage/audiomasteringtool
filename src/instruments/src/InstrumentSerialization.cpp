#include "amt/instruments/InstrumentSerialization.h"

#include <iomanip>
#include <sstream>

namespace amt::instruments {
namespace {
std::string escape_json(const std::string& value) {
  std::string escaped;
  for (const char character : value) {
    if (character == '\\' || character == '"') escaped.push_back('\\');
    if (character == '\n') escaped += "\\n";
    else if (character != '\n') escaped.push_back(character);
  }
  return escaped;
}
}
std::string instrument_events_to_json(const std::vector<InstrumentEvent>& events) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(4) << "[";
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (index != 0U) output << ',';
    const auto& event = events[index];
    output << "{\"schema_version\":" << event.schema_version
           << ",\"taxonomy_id\":\"" << escape_json(event.taxonomy_id)
           << "\",\"label\":\"" << escape_json(event.display_label)
           << "\",\"family\":\"" << escape_json(event.family)
           << "\",\"source_role\":\"" << source_role_name(event.source_role)
           << "\",\"confidence\":" << event.confidence
           << ",\"start_seconds\":" << event.start_seconds
           << ",\"end_seconds\":" << event.end_seconds
           << ",\"model_id\":\"" << escape_json(event.model_id)
           << "\",\"model_version\":\"" << escape_json(event.model_version) << "\"}";
  }
  output << "]";
  return output.str();
}
}  // namespace amt::instruments
