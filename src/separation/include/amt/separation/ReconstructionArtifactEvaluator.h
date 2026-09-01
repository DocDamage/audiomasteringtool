#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "amt/codec/AudioIO.h"
#include "amt/separation/SourceGuidance.h"

namespace amt::separation {

struct ReconstructionComparisonMetrics {
  std::uint64_t sample_count{0U};
  double residual_ratio{1.0};
  double correlation{0.0};
  double transient_mismatch_ratio{1.0};
  double high_frequency_mismatch_ratio{1.0};
  double measurement_confidence{0.0};
};

[[nodiscard]] ArtifactAssessment assess_reconstruction_comparison(
    const ReconstructionComparisonMetrics& metrics,
    double model_confidence);

class StreamingReconstructionArtifactEvaluator final
    : public IReconstructionArtifactEvaluator {
 public:
  explicit StreamingReconstructionArtifactEvaluator(amt::codec::ICodecService& codecs)
      : codecs_(codecs) {}

  [[nodiscard]] std::optional<ArtifactAssessment> evaluate(
      const std::filesystem::path& original_source,
      const SeparationResult& separation,
      std::string& error,
      const amt::core::CancellationToken* cancellation = nullptr,
      const amt::core::ProgressCallback& progress = {}) override;

 private:
  amt::codec::ICodecService& codecs_;
};

}  // namespace amt::separation
