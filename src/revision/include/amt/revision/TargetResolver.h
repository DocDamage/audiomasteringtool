#pragma once

#include <string>
#include <vector>
#include "amt/analysis/FileAnalyzer.h"
#include "amt/instruments/InstrumentEvent.h"
#include "amt/revision/RevisionIntent.h"

namespace amt::revision {

struct ResolvedTarget {
  TargetScope scope{TargetScope::global};
  std::string name;
  bool is_present{true};
  double confidence{1.0};
  std::string warning;
};

class TargetResolver {
 public:
  [[nodiscard]] static ResolvedTarget resolve(
      TargetScope scope,
      const std::string& target_name,
      const amt::analysis::Phase1AnalysisReport* analysis = nullptr,
      const std::vector<amt::instruments::InstrumentEvent>* instruments = nullptr);

  [[nodiscard]] static bool validate_targets(
      const RevisionIntent& intent,
      const amt::analysis::Phase1AnalysisReport* analysis,
      const std::vector<amt::instruments::InstrumentEvent>* instruments,
      std::vector<ResolvedTarget>& resolved,
      std::string& error);
};

}  // namespace amt::revision
