#include "amt/separation/Separation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace amt::separation {
namespace {

constexpr double kEvidenceThreshold = 0.35;

[[nodiscard]] double clamp01(const double value) {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] bool valid_sha256(const std::string& value) {
  if (value.size() != 64U) return false;
  return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
  });
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
  const auto value = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string& value) {
  std::u8string converted;
  converted.reserve(value.size());
  for (const unsigned char byte : value) converted.push_back(static_cast<char8_t>(byte));
  return std::filesystem::path(converted);
}

[[nodiscard]] std::string hex_encode(const std::string_view value) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string output;
  output.reserve(value.size() * 2U);
  for (const unsigned char byte : value) {
    output.push_back(digits[byte >> 4U]);
    output.push_back(digits[byte & 0x0FU]);
  }
  return output;
}

[[nodiscard]] int hex_value(const char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

[[nodiscard]] std::optional<std::string> hex_decode(const std::string& value) {
  if (value.size() % 2U != 0U) return std::nullopt;
  std::string output;
  output.reserve(value.size() / 2U);
  for (std::size_t index = 0U; index < value.size(); index += 2U) {
    const int high = hex_value(value[index]);
    const int low = hex_value(value[index + 1U]);
    if (high < 0 || low < 0) return std::nullopt;
    output.push_back(static_cast<char>((high << 4) | low));
  }
  return output;
}

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& component : path) {
    if (component == "..") return false;
  }
  return true;
}

[[nodiscard]] std::string artifact_kind_name(const CacheArtifactKind kind) {
  switch (kind) {
    case CacheArtifactKind::stem_audio: return "stem_audio";
    case CacheArtifactKind::time_frequency_mask: return "time_frequency_mask";
    case CacheArtifactKind::features: return "features";
  }
  return "features";
}

[[nodiscard]] std::optional<CacheArtifactKind> artifact_kind_from_name(const std::string& name) {
  if (name == "stem_audio") return CacheArtifactKind::stem_audio;
  if (name == "time_frequency_mask") return CacheArtifactKind::time_frequency_mask;
  if (name == "features") return CacheArtifactKind::features;
  return std::nullopt;
}

[[nodiscard]] std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0U;
  while (start <= line.size()) {
    const auto end = line.find('\t', start);
    if (end == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, end - start));
    start = end + 1U;
  }
  return fields;
}

[[nodiscard]] std::optional<double> parse_double(const std::string& value) {
  try {
    std::size_t consumed = 0U;
    const double parsed = std::stod(value, &consumed);
    return consumed == value.size() ? std::optional<double>(parsed) : std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<std::int64_t> parse_int64(const std::string& value) {
  try {
    std::size_t consumed = 0U;
    const auto parsed = std::stoll(value, &consumed);
    return consumed == value.size() ? std::optional<std::int64_t>(parsed) : std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return std::nullopt;
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

bool write_atomic(const std::filesystem::path& path, const std::string& content,
                  std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "unable to create separation cache directory: " + ec.message();
    return false;
  }

  auto temporary = path;
  temporary += ".tmp";
  auto backup = path;
  backup += ".bak";
  std::filesystem::remove(temporary, ec);
  ec.clear();
  std::filesystem::remove(backup, ec);
  ec.clear();

  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "unable to open separation cache manifest for writing";
      return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
      error = "failed while writing separation cache manifest";
      return false;
    }
  }

  const bool had_existing = std::filesystem::exists(path, ec) && !ec;
  ec.clear();
  if (had_existing) {
    std::filesystem::rename(path, backup, ec);
    if (ec) {
      error = "unable to stage existing separation cache manifest: " + ec.message();
      std::filesystem::remove(temporary, ec);
      return false;
    }
  }

  ec.clear();
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    const auto message = ec.message();
    if (had_existing) {
      std::error_code rollback_error;
      std::filesystem::rename(backup, path, rollback_error);
    }
    std::filesystem::remove(temporary, ec);
    error = "unable to finalize separation cache manifest: " + message;
    return false;
  }

  if (had_existing) {
    ec.clear();
    std::filesystem::remove(backup, ec);
  }
  return true;
}

[[nodiscard]] std::string manifest_text(const SeparationCacheEntry& entry) {
  std::ostringstream output;
  output << std::setprecision(17)
         << "schema=1\n"
         << "cache_key=" << hex_encode(canonical_cache_key(entry.key)) << '\n'
         << "sample_rate=" << entry.sample_rate << '\n'
         << "frames=" << entry.frames << '\n'
         << "overall_confidence=" << entry.overall_confidence << '\n'
         << "complete_reconstruction=" << (entry.complete_reconstruction ? 1 : 0) << '\n';
  for (const auto& artifact : entry.artifacts) {
    output << "artifact=" << artifact_kind_name(artifact.kind) << '\t'
           << stem_role_name(artifact.role) << '\t'
           << hex_encode(path_to_utf8(artifact.relative_path)) << '\t'
           << artifact.confidence << '\n';
  }
  return output.str();
}

void append_cache_field(std::ostringstream& output, const std::string& value) {
  output << value.size() << ':' << value << ';';
}

}  // namespace

ModelEligibility evaluate_model_for_bundled_production(
    const SeparationModelManifest& manifest) {
  ModelEligibility result;
  if (manifest.model_name.empty()) result.blockers.emplace_back("model name is missing");
  if (manifest.model_version.empty()) result.blockers.emplace_back("model version is missing");
  if (!valid_sha256(manifest.model_sha256)) result.blockers.emplace_back("model SHA-256 is missing or invalid");
  if (manifest.architecture_source.empty()) result.blockers.emplace_back("architecture/source is missing");
  if (manifest.weight_provenance.empty()) result.blockers.emplace_back("weight provenance is missing");
  if (manifest.code_license.empty()) result.blockers.emplace_back("code license is missing");
  if (manifest.weights_license.empty()) result.blockers.emplace_back("weights license is missing");
  if (!manifest.redistribution_reviewed) result.blockers.emplace_back("redistribution rights have not been reviewed");
  else if (!manifest.redistribution_allowed) result.blockers.emplace_back("weights are not approved for redistribution");
  if (!manifest.commercial_use_reviewed) result.blockers.emplace_back("commercial-use rights have not been reviewed");
  else if (!manifest.commercial_use_allowed) result.blockers.emplace_back("weights are not approved for commercial use");
  if (manifest.supported_execution_providers.empty()) result.blockers.emplace_back("execution providers are missing");
  if (manifest.expected_input_sample_rate <= 0) result.blockers.emplace_back("expected input sample rate is invalid");
  if (manifest.stem_taxonomy.empty()) result.blockers.emplace_back("stem taxonomy is missing");
  if (manifest.benchmark_record.empty()) result.blockers.emplace_back("benchmark record is missing");
  if (!manifest.security_reviewed) result.blockers.emplace_back("model security review is incomplete");
  result.eligible_for_bundled_production = result.blockers.empty();
  return result;
}

ArtifactAssessment assess_reconstruction_artifacts(
    const ReconstructionArtifactMetrics& metrics) {
  const double leakage = clamp01(metrics.leakage);
  const double musical_noise = clamp01(metrics.musical_noise);
  const double transient_damage = clamp01(metrics.transient_damage);
  const double phase_change = clamp01(metrics.phase_change);
  const double high_frequency_smearing = clamp01(metrics.high_frequency_smearing);
  const double source_residue = clamp01(metrics.source_residue);
  const double reconstruction_residual = clamp01(metrics.reconstruction_residual);
  const double model_confidence = clamp01(metrics.model_confidence);
  const double measurement_confidence = clamp01(metrics.measurement_confidence);

  const double weighted = 0.17 * leakage +
                          0.15 * musical_noise +
                          0.20 * transient_damage +
                          0.12 * phase_change +
                          0.12 * high_frequency_smearing +
                          0.12 * source_residue +
                          0.12 * reconstruction_residual;
  const double worst_artifact = std::max({leakage, musical_noise, transient_damage,
                                          phase_change, high_frequency_smearing,
                                          source_residue, reconstruction_residual});
  ArtifactAssessment result;
  result.overall_risk = clamp01(std::max(weighted, worst_artifact * 0.78) +
                                (1.0 - model_confidence) * 0.12);
  result.confidence = std::sqrt(model_confidence * measurement_confidence);

  if (leakage >= kEvidenceThreshold) result.evidence.emplace_back("source leakage is elevated");
  if (musical_noise >= kEvidenceThreshold) result.evidence.emplace_back("musical-noise artifact risk is elevated");
  if (transient_damage >= kEvidenceThreshold) result.evidence.emplace_back("transient preservation risk is elevated");
  if (phase_change >= kEvidenceThreshold) result.evidence.emplace_back("phase-change risk is elevated");
  if (high_frequency_smearing >= kEvidenceThreshold) result.evidence.emplace_back("high-frequency smearing risk is elevated");
  if (source_residue >= kEvidenceThreshold) result.evidence.emplace_back("source residue is elevated");
  if (reconstruction_residual >= kEvidenceThreshold) result.evidence.emplace_back("reconstruction residual energy is elevated");
  if (model_confidence < 0.65) result.evidence.emplace_back("separation model confidence is limited");
  if (measurement_confidence < 0.65) result.evidence.emplace_back("artifact assessment confidence is limited");
  if (result.evidence.empty()) result.evidence.emplace_back("no major reconstruction artifact indicator is elevated");
  return result;
}

SeparationDecision choose_separation_mode(
    const SourceInterventionEvidence& evidence,
    const ArtifactAssessment& artifact_assessment,
    const SeparationPolicyConfig& config) {
  SeparationDecision decision;
  decision.expected_benefit = clamp01(evidence.expected_repair_benefit);
  decision.artifact_risk = clamp01(artifact_assessment.overall_risk);

  if (!evidence.source_specific_issue) {
    decision.mode = SeparationMode::stereo_mastering;
    decision.confidence = 1.0;
    decision.reasons.emplace_back("no source-specific intervention is justified");
    return decision;
  }

  if (decision.expected_benefit < clamp01(config.minimum_expected_benefit)) {
    decision.mode = SeparationMode::stereo_mastering;
    decision.confidence = 1.0 - decision.expected_benefit;
    decision.reasons.emplace_back("expected source-specific benefit is too small");
    return decision;
  }

  const double model_confidence = clamp01(evidence.model_confidence);
  const double guidance_confidence = clamp01(evidence.source_guidance_confidence);
  const double guided_sufficiency = clamp01(evidence.source_guided_stereo_sufficiency);
  const double net_reconstruction_benefit = decision.expected_benefit - decision.artifact_risk;
  const bool reconstruction_quality_gate =
      model_confidence >= clamp01(config.minimum_reconstruction_model_confidence) &&
      artifact_assessment.confidence >= clamp01(config.minimum_artifact_assessment_confidence) &&
      decision.artifact_risk <= clamp01(config.maximum_reconstruction_artifact_risk) &&
      net_reconstruction_benefit >= clamp01(config.minimum_reconstruction_net_benefit);

  if (evidence.reconstruction_required_for_full_repair && reconstruction_quality_gate) {
    decision.mode = SeparationMode::stem_reconstruction;
    decision.confidence = std::min(model_confidence, artifact_assessment.confidence);
    decision.reasons.emplace_back("full repair requires reconstruction and all reconstruction quality gates passed");
    decision.reasons.emplace_back("expected repair benefit exceeds estimated reconstruction artifact cost");
    return decision;
  }

  const bool guidance_gate =
      guidance_confidence >= clamp01(config.minimum_guidance_confidence) &&
      guided_sufficiency >= clamp01(config.minimum_guided_stereo_sufficiency);
  if (guidance_gate) {
    decision.mode = SeparationMode::source_guided_stereo;
    decision.confidence = std::min(guidance_confidence,
                                   std::max(guided_sufficiency, artifact_assessment.confidence));
    decision.reasons.emplace_back("source estimates can guide processing on the original stereo mix");
    if (evidence.reconstruction_required_for_full_repair) {
      decision.reasons.emplace_back("stem reconstruction failed one or more quality gates, so the safer guided-stereo fallback was selected");
    } else {
      decision.reasons.emplace_back("reconstruction is unnecessary for the requested repair");
    }
    return decision;
  }

  decision.mode = SeparationMode::stereo_mastering;
  decision.confidence = std::max(0.25, 1.0 - guidance_confidence);
  decision.reasons.emplace_back("source-guidance confidence or guided-stereo sufficiency is too low");
  if (evidence.reconstruction_required_for_full_repair && !reconstruction_quality_gate) {
    decision.reasons.emplace_back("reconstruction quality gates did not justify replacing the original stereo mix");
  }
  return decision;
}

std::string canonical_cache_key(const SeparationCacheKey& key) {
  std::vector<std::string> stems;
  stems.reserve(key.requested_stems.size());
  for (const auto role : key.requested_stems) stems.push_back(stem_role_name(role));
  std::sort(stems.begin(), stems.end());
  stems.erase(std::unique(stems.begin(), stems.end()), stems.end());

  std::ostringstream output;
  output << "schema=" << key.schema_version << ';';
  append_cache_field(output, key.source_fingerprint);
  append_cache_field(output, key.model_name);
  append_cache_field(output, key.model_version);
  append_cache_field(output, key.model_sha256);
  output << "audio=" << (key.stem_audio ? 1 : 0) << ';'
         << "masks=" << (key.masks ? 1 : 0) << ';';
  for (const auto& stem : stems) append_cache_field(output, stem);
  return output.str();
}

std::string stable_cache_id(const SeparationCacheKey& key) {
  const auto canonical = canonical_cache_key(key);
  std::uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char byte : canonical) {
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

SeparationCache::SeparationCache(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path SeparationCache::entry_directory(const SeparationCacheKey& key) const {
  return root_ / stable_cache_id(key);
}

bool SeparationCache::prepare(const SeparationCacheKey& key, std::string& error) const {
  error.clear();
  if (key.source_fingerprint.empty() || key.model_name.empty() || key.model_version.empty() ||
      !valid_sha256(key.model_sha256)) {
    error = "separation cache key is incomplete";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(entry_directory(key), ec);
  if (ec) {
    error = "unable to create separation cache entry: " + ec.message();
    return false;
  }
  return true;
}

bool SeparationCache::mark_complete(const SeparationCacheEntry& entry,
                                    std::string& error) const {
  error.clear();
  if (!prepare(entry.key, error)) return false;
  if (entry.sample_rate <= 0 || entry.frames < 0 ||
      entry.overall_confidence < 0.0 || entry.overall_confidence > 1.0 ||
      entry.artifacts.empty()) {
    error = "separation cache entry metadata is incomplete";
    return false;
  }

  const auto directory = entry_directory(entry.key);
  for (const auto& artifact : entry.artifacts) {
    if (!safe_relative_path(artifact.relative_path)) {
      error = "separation cache artifact path must stay inside its cache entry";
      return false;
    }
    if (artifact.confidence < 0.0 || artifact.confidence > 1.0) {
      error = "separation cache artifact confidence is outside [0, 1]";
      return false;
    }
    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(directory / artifact.relative_path, exists_error) ||
        exists_error) {
      error = "separation cache artifact is missing: " + path_to_utf8(artifact.relative_path);
      return false;
    }
  }
  return write_atomic(directory / "manifest.amt", manifest_text(entry), error);
}

std::optional<SeparationCacheEntry> SeparationCache::load_complete(
    const SeparationCacheKey& key, std::string& error) const {
  error.clear();
  const auto directory = entry_directory(key);
  const auto manifest = read_text(directory / "manifest.amt");
  if (!manifest) return std::nullopt;

  SeparationCacheEntry entry;
  entry.key = key;
  bool schema_ok = false;
  bool key_ok = false;
  bool sample_rate_ok = false;
  bool frames_ok = false;
  bool confidence_ok = false;
  std::istringstream lines(*manifest);
  std::string line;
  while (std::getline(lines, line)) {
    if (line == "schema=1") {
      schema_ok = true;
      continue;
    }
    if (line.rfind("cache_key=", 0U) == 0U) {
      const auto decoded = hex_decode(line.substr(10U));
      key_ok = decoded && *decoded == canonical_cache_key(key);
      continue;
    }
    if (line.rfind("sample_rate=", 0U) == 0U) {
      const auto parsed = parse_int64(line.substr(12U));
      if (parsed && *parsed > 0) {
        entry.sample_rate = static_cast<int>(*parsed);
        sample_rate_ok = true;
      }
      continue;
    }
    if (line.rfind("frames=", 0U) == 0U) {
      const auto parsed = parse_int64(line.substr(7U));
      if (parsed && *parsed >= 0) {
        entry.frames = *parsed;
        frames_ok = true;
      }
      continue;
    }
    if (line.rfind("overall_confidence=", 0U) == 0U) {
      const auto parsed = parse_double(line.substr(19U));
      if (parsed && *parsed >= 0.0 && *parsed <= 1.0) {
        entry.overall_confidence = *parsed;
        confidence_ok = true;
      }
      continue;
    }
    if (line.rfind("complete_reconstruction=", 0U) == 0U) {
      const auto parsed = parse_int64(line.substr(24U));
      if (parsed && (*parsed == 0 || *parsed == 1)) {
        entry.complete_reconstruction = *parsed == 1;
      }
      continue;
    }
    if (line.rfind("artifact=", 0U) != 0U) continue;
    const auto fields = split_tabs(line.substr(9U));
    if (fields.size() != 4U) {
      error = "separation cache manifest contains an invalid artifact record";
      return std::nullopt;
    }
    const auto kind = artifact_kind_from_name(fields[0]);
    const auto path_text = hex_decode(fields[2]);
    const auto confidence = parse_double(fields[3]);
    if (!kind || !path_text || !confidence || *confidence < 0.0 || *confidence > 1.0) {
      error = "separation cache manifest contains an invalid artifact value";
      return std::nullopt;
    }
    const auto relative_path = path_from_utf8(*path_text);
    if (!safe_relative_path(relative_path)) {
      error = "separation cache manifest contains an unsafe artifact path";
      return std::nullopt;
    }
    entry.artifacts.push_back({.kind = *kind,
                               .role = stem_role_from_name(fields[1]),
                               .relative_path = relative_path,
                               .confidence = *confidence});
  }

  if (!schema_ok || !key_ok || !sample_rate_ok || !frames_ok || !confidence_ok ||
      entry.artifacts.empty()) {
    error = "separation cache manifest is incomplete or does not match the requested cache key";
    return std::nullopt;
  }

  for (const auto& artifact : entry.artifacts) {
    std::error_code exists_error;
    const bool exists = std::filesystem::is_regular_file(directory / artifact.relative_path,
                                                         exists_error);
    if (exists_error) {
      error = "unable to inspect cached separation artifact: " + exists_error.message();
      return std::nullopt;
    }
    if (!exists) {
      error.clear();
      return std::nullopt;
    }
  }
  return entry;
}

std::string stem_role_name(const StemRole role) {
  switch (role) {
    case StemRole::unknown: return "unknown";
    case StemRole::vocals: return "vocals";
    case StemRole::drums: return "drums";
    case StemRole::bass: return "bass";
    case StemRole::other: return "other";
    case StemRole::kick: return "kick";
    case StemRole::snare: return "snare";
    case StemRole::percussion: return "percussion";
    case StemRole::tonal: return "tonal";
  }
  return "unknown";
}

StemRole stem_role_from_name(const std::string& name) {
  if (name == "vocals") return StemRole::vocals;
  if (name == "drums") return StemRole::drums;
  if (name == "bass") return StemRole::bass;
  if (name == "other") return StemRole::other;
  if (name == "kick") return StemRole::kick;
  if (name == "snare") return StemRole::snare;
  if (name == "percussion") return StemRole::percussion;
  if (name == "tonal") return StemRole::tonal;
  return StemRole::unknown;
}

std::string separation_mode_name(const SeparationMode mode) {
  switch (mode) {
    case SeparationMode::stereo_mastering: return "stereo_mastering";
    case SeparationMode::source_guided_stereo: return "source_guided_stereo";
    case SeparationMode::stem_reconstruction: return "stem_reconstruction";
  }
  return "stereo_mastering";
}

}  // namespace amt::separation
