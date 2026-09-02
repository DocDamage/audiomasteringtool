#include "amt/revision/TargetResolver.h"

namespace amt::revision {

ResolvedTarget TargetResolver::resolve(
    TargetScope scope,
    const std::string& target_name,
    const amt::analysis::Phase1AnalysisReport* /*analysis*/,
    const std::vector<amt::instruments::InstrumentEvent>* instruments) {
  ResolvedTarget target;
  target.scope = scope;
  target.name = target_name;
  target.is_present = true;
  target.confidence = 1.0;

  if (scope == TargetScope::global) {
    return target;
  }

  // Check detected instruments if provided
  if (instruments && !instruments->empty()) {
    bool found = false;
    double max_conf = 0.0;

    for (const auto& ev : *instruments) {
      if (scope == TargetScope::instrument_808 && (ev.taxonomy_id.find("808") != std::string::npos || ev.display_label.find("808") != std::string::npos)) {
        found = true;
        if (ev.confidence > max_conf) max_conf = ev.confidence;
      } else if (scope == TargetScope::instrument_kick && (ev.taxonomy_id.find("kick") != std::string::npos || ev.display_label.find("Kick") != std::string::npos)) {
        found = true;
        if (ev.confidence > max_conf) max_conf = ev.confidence;
      } else if (scope == TargetScope::instrument_snare && (ev.taxonomy_id.find("snare") != std::string::npos || ev.display_label.find("Snare") != std::string::npos)) {
        found = true;
        if (ev.confidence > max_conf) max_conf = ev.confidence;
      } else if ((scope == TargetScope::instrument_lead_vocal || scope == TargetScope::stem_vocal) &&
                 (ev.taxonomy_id.find("vocal") != std::string::npos || ev.source_role == amt::instruments::SourceRole::vocals)) {
        found = true;
        if (ev.confidence > max_conf) max_conf = ev.confidence;
      }
    }

    if (found) {
      target.is_present = true;
      target.confidence = max_conf;
    } else {
      target.is_present = false;
      target.confidence = 0.2;
      target.warning = target_name + " requested but not confidently detected in the mix";
    }
  }

  return target;
}

bool TargetResolver::validate_targets(
    const RevisionIntent& intent,
    const amt::analysis::Phase1AnalysisReport* analysis,
    const std::vector<amt::instruments::InstrumentEvent>* instruments,
    std::vector<ResolvedTarget>& resolved,
    std::string& error) {
  error.clear();
  resolved.clear();

  for (const auto& action : intent.actions) {
    auto res = resolve(action.target, action.target_name, analysis, instruments);
    resolved.push_back(res);
  }

  return true;
}

}  // namespace amt::revision
