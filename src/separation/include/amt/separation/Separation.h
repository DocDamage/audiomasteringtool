#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/core/JobControl.h"

namespace amt::separation {

enum class SeparationMode {
  stereo_mastering = 0,
  source_guided_stereo = 1,
  stem_reconstruction = 2
};

enum class StemRole {
  unknown,
  vocals,
  drums,
  bass,
  other,
  kick,
  snare,
  percussion,
  tonal
};

enum class CacheArtifactKind { stem_audio, time_frequency_mask, features };

struct SeparationModelManifest {
  std::string model_name;
  std::string model_version;
  std::string model_sha256;
  std::string architecture_source;
  std::string weight_provenance;
  std::string code_license;
  std::string weights_license;
  bool redistribution_reviewed{false};
  bool redistribution_allowed{false};
  bool commercial_use_reviewed{false};
  bool commercial_use_allowed{false};
  std::string attribution_requirements;
  std::vector<std::string> supported_execution_providers;
  int expected_input_sample_rate{0};
  std::vector<StemRole> stem_taxonomy;
  std::string benchmark_record;
  bool security_reviewed{false};
};

struct ModelEligibility {
  bool eligible_for_bundled_production{false};
  std::vector<std::string> blockers;
};

[[nodiscard]] ModelEligibility evaluate_model_for_bundled_production(
    const SeparationModelManifest& manifest);

struct SeparationRequest {
  std::filesystem::path source_path;
  std::filesystem::path cache_directory;
  std::vector<StemRole> requested_stems;
  bool request_stem_audio{true};
  bool request_time_frequency_masks{true};
};

struct SeparationArtifactReference {
  CacheArtifactKind kind{CacheArtifactKind::stem_audio};
  StemRole role{StemRole::unknown};
  std::filesystem::path path;
  double confidence{0.0};
};

struct SeparationResult {
  SeparationModelManifest model;
  int sample_rate{0};
  std::int64_t frames{0};
  double overall_confidence{0.0};
  std::vector<SeparationArtifactReference> artifacts;
};

class ISeparationProvider {
 public:
  virtual ~ISeparationProvider() = default;
  [[nodiscard]] virtual bool available() const noexcept = 0;
  [[nodiscard]] virtual SeparationModelManifest model_manifest() const = 0;
  [[nodiscard]] virtual std::optional<SeparationResult> separate(
      const SeparationRequest& request, std::string& error,
      const amt::core::CancellationToken* cancellation = nullptr,
      const amt::core::ProgressCallback& progress = {}) = 0;
};

struct ReconstructionArtifactMetrics {
  double leakage{0.0};
  double musical_noise{0.0};
  double transient_damage{0.0};
  double phase_change{0.0};
  double high_frequency_smearing{0.0};
  double source_residue{0.0};
  double reconstruction_residual{0.0};
  double model_confidence{0.0};
  double measurement_confidence{0.0};
};

struct ArtifactAssessment {
  double overall_risk{1.0};
  double confidence{0.0};
  std::vector<std::string> evidence;
};

[[nodiscard]] ArtifactAssessment assess_reconstruction_artifacts(
    const ReconstructionArtifactMetrics& metrics);

struct SourceInterventionEvidence {
  bool source_specific_issue{false};
  bool reconstruction_required_for_full_repair{false};
  double expected_repair_benefit{0.0};
  double source_guidance_confidence{0.0};
  double source_guided_stereo_sufficiency{0.0};
  double model_confidence{0.0};
};

struct SeparationPolicyConfig {
  double minimum_expected_benefit{0.18};
  double minimum_guidance_confidence{0.55};
  double minimum_guided_stereo_sufficiency{0.45};
  double minimum_reconstruction_model_confidence{0.80};
  double maximum_reconstruction_artifact_risk{0.25};
  double minimum_reconstruction_net_benefit{0.20};
};

struct SeparationDecision {
  SeparationMode mode{SeparationMode::stereo_mastering};
  double expected_benefit{0.0};
  double artifact_risk{1.0};
  double confidence{0.0};
  std::vector<std::string> reasons;
};

[[nodiscard]] SeparationDecision choose_separation_mode(
    const SourceInterventionEvidence& evidence,
    const ArtifactAssessment& artifact_assessment,
    const SeparationPolicyConfig& config = {});

struct SeparationCacheKey {
  int schema_version{1};
  std::string source_fingerprint;
  std::string model_name;
  std::string model_version;
  std::string model_sha256;
  std::vector<StemRole> requested_stems;
  bool stem_audio{true};
  bool masks{true};
};

struct CachedArtifact {
  CacheArtifactKind kind{CacheArtifactKind::stem_audio};
  StemRole role{StemRole::unknown};
  std::filesystem::path relative_path;
};

struct SeparationCacheEntry {
  SeparationCacheKey key;
  std::vector<CachedArtifact> artifacts;
};

[[nodiscard]] std::string canonical_cache_key(const SeparationCacheKey& key);
[[nodiscard]] std::string stable_cache_id(const SeparationCacheKey& key);

class SeparationCache {
 public:
  explicit SeparationCache(std::filesystem::path root);

  [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
  [[nodiscard]] std::filesystem::path entry_directory(const SeparationCacheKey& key) const;
  bool prepare(const SeparationCacheKey& key, std::string& error) const;
  bool mark_complete(const SeparationCacheEntry& entry, std::string& error) const;
  [[nodiscard]] std::optional<SeparationCacheEntry> load_complete(
      const SeparationCacheKey& key, std::string& error) const;

 private:
  std::filesystem::path root_;
};

[[nodiscard]] std::string stem_role_name(StemRole role);
[[nodiscard]] StemRole stem_role_from_name(const std::string& name);
[[nodiscard]] std::string separation_mode_name(SeparationMode mode);

}  // namespace amt::separation
