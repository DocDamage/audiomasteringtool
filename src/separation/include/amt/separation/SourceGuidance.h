#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/separation/Separation.h"

namespace amt::separation {

class IReconstructionArtifactEvaluator {
 public:
  virtual ~IReconstructionArtifactEvaluator() = default;
  [[nodiscard]] virtual std::optional<ArtifactAssessment> evaluate(
      const std::filesystem::path& original_source,
      const SeparationResult& separation,
      std::string& error,
      const amt::core::CancellationToken* cancellation = nullptr,
      const amt::core::ProgressCallback& progress = {}) = 0;
};

struct SourceGuidanceRequest {
  SeparationRequest separation;
  SeparationCacheKey cache_key;
  SourceInterventionEvidence evidence;
};

struct SourceGuidanceConfig {
  SeparationPolicyConfig policy;
  bool require_bundled_production_model_eligibility{true};
  bool enable_cache{true};
  bool compute_missing_source_fingerprint{true};

  // Allows a provider invocation solely to gather source-estimate evidence. This
  // must not itself authorize an intervention: the returned decision still uses
  // the supplied evidence, so callers must analyze the estimates and re-run policy
  // before selecting Mode 1. Default false preserves the original contract.
  bool allow_diagnostic_separation{false};
};

struct SourceGuidanceResult {
  SeparationDecision decision;
  std::optional<SeparationResult> separation;
  std::optional<ArtifactAssessment> artifact_assessment;
  bool provider_invoked{false};
  bool cache_hit{false};
  std::vector<std::string> warnings;
};

class SourceGuidanceOrchestrator {
 public:
  SourceGuidanceOrchestrator(ISeparationProvider& provider,
                             IReconstructionArtifactEvaluator* artifact_evaluator = nullptr,
                             SeparationCache* cache = nullptr)
      : provider_(provider), artifact_evaluator_(artifact_evaluator), cache_(cache) {}

  [[nodiscard]] std::optional<SourceGuidanceResult> execute(
      const SourceGuidanceRequest& request,
      std::string& error,
      const SourceGuidanceConfig& config = {},
      const amt::core::CancellationToken* cancellation = nullptr,
      const amt::core::ProgressCallback& progress = {});

 private:
  ISeparationProvider& provider_;
  IReconstructionArtifactEvaluator* artifact_evaluator_{nullptr};
  SeparationCache* cache_{nullptr};
};

}  // namespace amt::separation
