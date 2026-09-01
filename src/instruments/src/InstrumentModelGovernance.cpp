#include "amt/instruments/InstrumentModelGovernance.h"

namespace amt::instruments {
InstrumentModelEligibility evaluate_instrument_model_for_production(const InstrumentModelManifest& model) {
  InstrumentModelEligibility result;
  if (model.model_id.empty() || model.version.empty() || model.artifact_sha256.size() != 64U) result.blockers.push_back("stable model identity and SHA-256 are required");
  if (model.artifact_size_bytes == 0U || model.input_sample_rate <= 0 || model.input_window_seconds <= 0.0) result.blockers.push_back("artifact and input contract are incomplete");
  if (model.code_license.empty() || model.weights_license.empty() || !model.commercial_use_reviewed || !model.redistribution_reviewed) result.blockers.push_back("license/commercial/redistribution review is incomplete");
  if (!model.security_reviewed || model.output_vocabulary_version.empty()) result.blockers.push_back("security or vocabulary review is incomplete");
  if (model.evaluation_record.empty() || model.calibration_record.empty()) result.blockers.push_back("frozen evaluation and confidence calibration records are required");
  result.production_eligible = result.blockers.empty(); return result;
}
}  // namespace amt::instruments
