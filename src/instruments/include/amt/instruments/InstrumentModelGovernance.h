#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace amt::instruments {

struct InstrumentModelManifest {
  std::string model_id;
  std::string version;
  std::string artifact_sha256;
  std::uint64_t artifact_size_bytes{0};
  std::string code_license;
  std::string weights_license;
  bool commercial_use_reviewed{false};
  bool redistribution_reviewed{false};
  bool security_reviewed{false};
  int input_sample_rate{0};
  double input_window_seconds{0.0};
  std::string output_vocabulary_version;
  std::string evaluation_record;
  std::string calibration_record;
};

struct InstrumentModelEligibility { bool production_eligible{false}; std::vector<std::string> blockers; };
[[nodiscard]] InstrumentModelEligibility evaluate_instrument_model_for_production(
    const InstrumentModelManifest& manifest);
}  // namespace amt::instruments
