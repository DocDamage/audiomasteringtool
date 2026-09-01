#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/core/JobControl.h"
#include "amt/instruments/InstrumentEvent.h"

namespace amt::instruments {

struct InstrumentModelInfo {
  std::string model_id;
  std::string model_version;
  std::string artifact_sha256;
  std::string output_vocabulary_version;
  int input_sample_rate{0};
  double window_seconds{0.0};
  bool calibration_reviewed{false};
  bool production_approved{false};
};

struct InstrumentDetectionRequest {
  std::filesystem::path canonical_stereo_path;
  std::optional<std::filesystem::path> optional_stem_path;
  SourceRole stem_role{SourceRole::unknown};
  double start_seconds{0.0};
  double end_seconds{0.0};
};

struct InstrumentDetectionResult { std::vector<InstrumentEvent> events; };

class IInstrumentDetector {
 public:
  virtual ~IInstrumentDetector() = default;
  [[nodiscard]] virtual bool available() const noexcept = 0;
  [[nodiscard]] virtual InstrumentModelInfo info() const = 0;
  [[nodiscard]] virtual std::optional<InstrumentDetectionResult> detect(
      const InstrumentDetectionRequest& request, std::string& error,
      const amt::core::CancellationToken* cancellation = nullptr,
      const amt::core::ProgressCallback& progress = {}) = 0;
};

}  // namespace amt::instruments
