#pragma once

#include <string>
#include <vector>

#include "amt/instruments/InstrumentTaxonomy.h"

namespace amt::instruments {

struct InstrumentAttributes {
  double acoustic{0.0};
  double electric{0.0};
  double synthetic{0.0};
  double sampled{0.0};
  double distorted{0.0};
  double reverberant{0.0};
  double transient{0.0};
  double sustained{0.0};
};

struct InstrumentEvent {
  int schema_version{1};
  std::string taxonomy_id{"unknown"};
  std::string display_label{"Unknown"};
  std::string family;
  SourceRole source_role{SourceRole::unknown};
  double confidence{0.0};
  double start_seconds{0.0};
  double end_seconds{0.0};
  std::vector<double> active_frequency_hz;
  InstrumentAttributes attributes;
  std::string stem_association;
  std::string model_id;
  std::string model_version;
  std::vector<std::string> evidence;
};

}  // namespace amt::instruments
