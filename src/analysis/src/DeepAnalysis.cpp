#include "amt/analysis/DeepAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>

namespace amt::analysis {
namespace {

constexpr std::size_t kAnalysisFrames = 8192U;

double clamp_score(const double value) { return std::clamp(value, 0.0, 100.0); }

double band_ratio(const SpectrumMetrics& spectrum, const std::size_t index) {
  return index < spectrum.bands.size() ? spectrum.bands[index].energy_ratio : 0.0;
}

std::string assessment_name(const double score) {
  if (score >= 85.0) return "strong";
  if (score >= 70.0) return "generally_balanced";
  if (score >= 50.0) return "attention_warranted";
  return "high_risk";
}

std::string format_number(const double value, const int precision = 2) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << value;
  return stream.str();
}

std::string escape_json(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (const char value : input) {
    switch (value) {
      case '\\': output += "\\\\"; break;
      case '"': output += "\\\""; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default: output += value; break;
    }
  }
  return output;
}

MixHealthAssessment build_mix_health(const AnalysisReport& report) {
  MixHealthAssessment health;
  const double resonance_penalty = report.perceptual.resonances.empty()
      ? 0.0 : report.perceptual.resonances.front().severity * 18.0;
  const double tonal = clamp_score(
      100.0 - report.perceptual.tonal_imbalance_score * 48.0 - resonance_penalty);

  const double macro = report.structural.macro_dynamics.macro_dynamic_range_db;
  const double plr = report.technical.loudness.peak_to_loudness_ratio_db;
  double dynamics_penalty = 0.0;
  if (macro < 3.0) dynamics_penalty += (3.0 - macro) * 8.0;
  if (macro > 18.0) dynamics_penalty += (macro - 18.0) * 2.0;
  if (plr < 6.0) dynamics_penalty += (6.0 - plr) * 7.0;
  if (report.technical.loudness.short_term_variation_lu > 9.0) {
    dynamics_penalty += (report.technical.loudness.short_term_variation_lu - 9.0) * 2.5;
  }
  const double dynamics = clamp_score(100.0 - dynamics_penalty);

  const double crest = report.technical.loudness.crest_factor_db;
  double transient_penalty = 0.0;
  if (crest < 6.0) transient_penalty += (6.0 - crest) * 8.0;
  if (report.structural.tempo.transient_fraction < 0.01 &&
      report.structural.tempo.onset_density_per_second < 0.15) {
    transient_penalty += 8.0;
  }
  const double transients = clamp_score(100.0 - transient_penalty);

  double stereo_penalty = 0.0;
  if (report.technical.metadata.channels >= 2) {
    if (report.technical.stereo.correlation < 0.0) stereo_penalty += 35.0;
    else if (report.technical.stereo.correlation < 0.2) stereo_penalty += 16.0;
    stereo_penalty += std::clamp(report.technical.stereo.negative_correlation_window_fraction, 0.0, 1.0) * 35.0;
    if (report.technical.stereo.low_band_width > 0.25) stereo_penalty += 15.0;
    if (report.technical.stereo.mono_fold_down_delta_db < -3.0) {
      stereo_penalty += std::min(30.0, std::abs(report.technical.stereo.mono_fold_down_delta_db + 3.0) * 5.0);
    }
  }
  const double stereo = clamp_score(100.0 - stereo_penalty);

  double integrity_penalty = 0.0;
  integrity_penalty += (report.technical.integrity.nan_samples > 0U ||
                        report.technical.integrity.infinite_samples > 0U) ? 80.0 : 0.0;
  integrity_penalty += std::clamp(report.technical.integrity.max_absolute_dc_offset / 0.02, 0.0, 1.0) * 25.0;
  integrity_penalty += report.character.accidental_defect_risk * 45.0;
  if (report.technical.loudness.true_peak_dbtp > 1.0) {
    integrity_penalty += std::min(20.0, report.technical.loudness.true_peak_dbtp * 6.0);
  }
  const double integrity = clamp_score(100.0 - integrity_penalty);

  const double structural_confidence = std::clamp(
      0.45 + static_cast<double>(report.structural.sections.size()) * 0.05, 0.45, 0.90);
  const double character_confidence = report.character.inference_confidence;
  health.dimensions = {
      {.id = "tonal_balance", .label = "Tonal balance", .heuristic_score = tonal,
       .confidence = 0.78, .assessment = assessment_name(tonal),
       .rationale = "Based on broad-band balance, persistent resonance, and harshness/mud/sub concentration."},
      {.id = "dynamics", .label = "Dynamics", .heuristic_score = dynamics,
       .confidence = 0.86, .assessment = assessment_name(dynamics),
       .rationale = "Based on PLR, macro RMS spread, and short-term loudness variation."},
      {.id = "transients", .label = "Transient health", .heuristic_score = transients,
       .confidence = structural_confidence, .assessment = assessment_name(transients),
       .rationale = "Based on crest factor and time-local onset/transient density."},
      {.id = "stereo_translation", .label = "Stereo / mono translation", .heuristic_score = stereo,
       .confidence = report.technical.metadata.channels >= 2 ? 0.88 : 0.65,
       .assessment = assessment_name(stereo),
       .rationale = "Based on correlation, negative-correlation windows, band width, and mono fold-down behavior."},
      {.id = "technical_integrity", .label = "Technical integrity", .heuristic_score = integrity,
       .confidence = std::max(0.75, character_confidence), .assessment = assessment_name(integrity),
       .rationale = "Based on invalid samples, DC, clipping pattern, true peak, and defect-vs-character heuristics."}};

  const std::array<double, 5> weights = {0.25, 0.22, 0.16, 0.17, 0.20};
  double weighted = 0.0;
  double confidence = 0.0;
  for (std::size_t index = 0U; index < health.dimensions.size(); ++index) {
    weighted += health.dimensions[index].heuristic_score * weights[index];
    confidence += health.dimensions[index].confidence * weights[index];
  }
  health.overall_heuristic_score = clamp_score(weighted);
  health.overall_confidence = std::clamp(confidence, 0.0, 1.0);
  health.overall_assessment = assessment_name(health.overall_heuristic_score);
  return health;
}

void add_finding(std::vector<AnalysisFinding>& findings, AnalysisFinding finding) {
  finding.confidence = std::clamp(finding.confidence, 0.0, 1.0);
  findings.push_back(std::move(finding));
}

std::vector<AnalysisFinding> build_findings(const AnalysisReport& report) {
  std::vector<AnalysisFinding> findings;
  const auto& technical = report.technical;

  if (technical.integrity.nan_samples > 0U || technical.integrity.infinite_samples > 0U) {
    add_finding(findings, {.id = "invalid_samples", .category = FindingCategory::technical,
      .severity = FindingSeverity::high, .confidence = 1.0, .heuristic = false,
      .title = "Invalid floating-point samples detected",
      .detail = "The source contains NaN or infinite samples and should be repaired before mastering.",
      .evidence = {"NaN samples: " + std::to_string(technical.integrity.nan_samples),
                   "Infinite samples: " + std::to_string(technical.integrity.infinite_samples)}});
  }
  if (technical.integrity.max_absolute_dc_offset > 0.003) {
    const auto severity = technical.integrity.max_absolute_dc_offset > 0.015
        ? FindingSeverity::high : FindingSeverity::moderate;
    add_finding(findings, {.id = "dc_offset", .category = FindingCategory::technical,
      .severity = severity, .confidence = 0.98, .heuristic = false,
      .title = "DC offset is elevated",
      .detail = "Removing ultra-low/DC energy before dynamics processing may improve headroom.",
      .evidence = {"Maximum absolute DC offset: " +
                   format_number(technical.integrity.max_absolute_dc_offset, 5)}});
  }

  if (report.character.accidental_defect_risk >= 0.55) {
    add_finding(findings, {.id = "clipping_defect_risk", .category = FindingCategory::character,
      .severity = report.character.accidental_defect_risk >= 0.75 ? FindingSeverity::high
                                                                  : FindingSeverity::moderate,
      .confidence = report.character.inference_confidence, .heuristic = true,
      .title = "Clipping pattern may be accidental",
      .detail = "The clipping/flat-top pattern is localized or severe enough to justify cautious repair evaluation.",
      .evidence = {"Hard-clip likelihood: " + format_number(report.character.hard_clip_likelihood),
                   "Clipping-window fraction: " + format_number(report.character.clipping_window_fraction),
                   "Accidental-defect risk: " + format_number(report.character.accidental_defect_risk)}});
  } else if (report.character.intentional_character_likelihood >= 0.60) {
    add_finding(findings, {.id = "intentional_character", .category = FindingCategory::character,
      .severity = FindingSeverity::info, .confidence = report.character.inference_confidence,
      .heuristic = true, .title = "Clipping/saturation appears distributed rather than isolated",
      .detail = "The pattern is consistent with intentional loudness, saturation, or drum/sample character. Preserve it unless another problem justifies intervention.",
      .evidence = {"Intentional-character likelihood: " +
                   format_number(report.character.intentional_character_likelihood),
                   "Saturation likelihood: " + format_number(report.character.saturation_likelihood)}});
  }

  if (report.perceptual.sub_buildup_score >= 0.65) {
    add_finding(findings, {.id = "sub_buildup", .category = FindingCategory::tonal,
      .severity = FindingSeverity::moderate, .confidence = 0.78, .heuristic = true,
      .title = "Sub-bass is unusually dominant",
      .detail = "Low-frequency energy may consume headroom and increase limiter pumping. Prefer dynamic or source-aware control over a broad bass cut.",
      .evidence = {"Sub-buildup score: " + format_number(report.perceptual.sub_buildup_score),
                   "20–60 Hz ratio: " + format_number(band_ratio(technical.spectrum, 0U), 3),
                   "60–250 Hz ratio: " + format_number(band_ratio(technical.spectrum, 1U), 3)}});
  }
  if (report.perceptual.mud_score >= 0.65) {
    add_finding(findings, {.id = "low_mid_mud", .category = FindingCategory::tonal,
      .severity = FindingSeverity::moderate, .confidence = 0.76, .heuristic = true,
      .title = "Low-mid density may be masking definition",
      .detail = "Persistent energy in the low-mid region is high relative to the rest of the spectrum.",
      .evidence = {"Mud score: " + format_number(report.perceptual.mud_score)}});
  }
  if (report.perceptual.harshness_score >= 0.62) {
    add_finding(findings, {.id = "harshness", .category = FindingCategory::tonal,
      .severity = report.perceptual.harshness_score >= 0.82 ? FindingSeverity::high
                                                           : FindingSeverity::moderate,
      .confidence = 0.80, .heuristic = true,
      .title = "Upper-mid harshness risk is elevated",
      .detail = "Energy and persistent narrow peaks in the 2.5–6.5 kHz area may become fatiguing when the master is pushed louder.",
      .evidence = {"Harshness score: " + format_number(report.perceptual.harshness_score)}});
  }

  for (std::size_t index = 0U; index < std::min<std::size_t>(3U, report.perceptual.resonances.size()); ++index) {
    const auto& resonance = report.perceptual.resonances[index];
    if (resonance.severity < 0.50) continue;
    add_finding(findings, {.id = "resonance_" + std::to_string(index),
      .category = FindingCategory::tonal,
      .severity = resonance.severity >= 0.78 ? FindingSeverity::high : FindingSeverity::moderate,
      .confidence = std::clamp(0.55 + resonance.persistence * 0.4, 0.0, 0.95), .heuristic = true,
      .has_time_range = resonance.last_seen_seconds > resonance.first_seen_seconds,
      .start_seconds = resonance.first_seen_seconds, .end_seconds = resonance.last_seen_seconds,
      .title = "Persistent resonance near " + format_number(resonance.frequency_hz, 0) + " Hz",
      .detail = "A narrow spectral peak persists often enough to be a candidate for dynamic rather than static control.",
      .evidence = {"Prominence: " + format_number(resonance.prominence_db) + " dB",
                   "Persistence: " + format_number(resonance.persistence)}});
  }

  if (technical.metadata.channels >= 2 && technical.stereo.low_band_width > 0.25) {
    add_finding(findings, {.id = "wide_low_end", .category = FindingCategory::stereo,
      .severity = FindingSeverity::moderate, .confidence = 0.88, .heuristic = false,
      .title = "Low-frequency stereo width is high",
      .detail = "The low end may lose weight or change significantly in mono. Keep sub-bass centered during mastering.",
      .evidence = {"Low-band width: " + format_number(technical.stereo.low_band_width),
                   "Mono fold-down delta: " + format_number(technical.stereo.mono_fold_down_delta_db) + " dB"}});
  }
  if (technical.metadata.channels >= 2 &&
      (technical.stereo.correlation < 0.0 || technical.stereo.negative_correlation_window_fraction > 0.20)) {
    add_finding(findings, {.id = "phase_instability", .category = FindingCategory::stereo,
      .severity = FindingSeverity::high, .confidence = 0.92, .heuristic = false,
      .title = "Phase instability may hurt mono translation",
      .detail = "Negative correlation is frequent enough that widening should be avoided until the affected material is understood.",
      .evidence = {"Global correlation: " + format_number(technical.stereo.correlation),
                   "Negative-correlation window fraction: " +
                   format_number(technical.stereo.negative_correlation_window_fraction)}});
  }

  if (technical.loudness.peak_to_loudness_ratio_db < 6.0 ||
      technical.loudness.crest_factor_db < 6.0) {
    add_finding(findings, {.id = "flattened_dynamics", .category = FindingCategory::dynamics,
      .severity = FindingSeverity::moderate, .confidence = 0.88, .heuristic = true,
      .title = "Transient/dynamic headroom is already limited",
      .detail = "The mix is dense enough that aggressive additional compression or limiting risks flattening attacks.",
      .evidence = {"PLR: " + format_number(technical.loudness.peak_to_loudness_ratio_db) + " dB",
                   "Crest factor: " + format_number(technical.loudness.crest_factor_db) + " dB"}});
  }
  if (report.structural.macro_dynamics.section_contrast_db > 9.0) {
    add_finding(findings, {.id = "large_section_contrast", .category = FindingCategory::structure,
      .severity = FindingSeverity::low, .confidence = 0.80, .heuristic = false,
      .title = "Sections have substantial level contrast",
      .detail = "Mastering should preserve intentional arrangement contrast while preventing the loudest section from dominating the limiter.",
      .evidence = {"Section contrast: " +
                   format_number(report.structural.macro_dynamics.section_contrast_db) + " dB",
                   "Detected segments: " + std::to_string(report.structural.sections.size())}});
  }
  if (report.structural.tempo.confidence >= 0.55) {
    add_finding(findings, {.id = "tempo_structure", .category = FindingCategory::structure,
      .severity = FindingSeverity::info, .confidence = report.structural.tempo.confidence,
      .heuristic = true, .title = "Rhythmic pulse detected near " +
          format_number(report.structural.tempo.bpm, 1) + " BPM",
      .detail = "This tempo estimate can anchor later beat-aware and section-aware analysis.",
      .evidence = {"Onset density: " +
                   format_number(report.structural.tempo.onset_density_per_second) + " events/s"}});
  }

  const bool actionable = std::any_of(findings.begin(), findings.end(), [](const AnalysisFinding& finding) {
    return finding.severity == FindingSeverity::moderate || finding.severity == FindingSeverity::high;
  });
  if (!actionable) {
    add_finding(findings, {.id = "no_major_issue", .category = FindingCategory::technical,
      .severity = FindingSeverity::info, .confidence = 0.78, .heuristic = true,
      .title = "No high-confidence major technical problem found",
      .detail = "The deterministic analysis suggests light finishing is safer than broad corrective processing."});
  }

  std::stable_sort(findings.begin(), findings.end(), [](const AnalysisFinding& a, const AnalysisFinding& b) {
    return static_cast<int>(a.severity) > static_cast<int>(b.severity);
  });
  return findings;
}

}  // namespace

std::optional<AnalysisReport> analyze_track(
    amt::codec::ICodecService& codecs, const std::filesystem::path& path,
    std::string& error, const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  const auto technical = analyze_file(
      codecs, path, error, cancellation,
      [&](const double value) { amt::core::report_progress(progress, value * 0.50); });
  if (!technical) return std::nullopt;

  auto decoder = codecs.open_decoder(path, error);
  if (!decoder) return std::nullopt;
  const auto metadata = decoder->metadata();
  StructuralAnalyzer structural(metadata.sample_rate, static_cast<std::size_t>(metadata.channels));
  PerceptualAnalyzer perceptual(metadata.sample_rate, static_cast<std::size_t>(metadata.channels));
  CharacterAnalyzer character(metadata.sample_rate, static_cast<std::size_t>(metadata.channels));

  std::int64_t consumed = 0;
  while (true) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "deep analysis cancelled";
      return std::nullopt;
    }
    amt::audio::AudioBuffer buffer;
    std::size_t frames = 0U;
    if (!decoder->read(buffer, kAnalysisFrames, frames, error, cancellation)) return std::nullopt;
    if (frames == 0U) break;
    structural.process(buffer);
    perceptual.process(buffer);
    character.process(buffer);
    consumed += static_cast<std::int64_t>(frames);
    if (metadata.frames > 0) {
      amt::core::report_progress(progress, 0.50 + 0.46 *
          static_cast<double>(consumed) / static_cast<double>(metadata.frames));
    }
  }

  AnalysisReport report;
  report.technical = *technical;
  report.structural = structural.finalize();
  report.perceptual = perceptual.finalize();
  report.character = character.finalize(report.technical.integrity);
  report.mix_health = build_mix_health(report);
  report.findings = build_findings(report);
  amt::core::report_progress(progress, 1.0);
  return report;
}

std::string finding_severity_name(const FindingSeverity severity) {
  switch (severity) {
    case FindingSeverity::info: return "info";
    case FindingSeverity::low: return "low";
    case FindingSeverity::moderate: return "moderate";
    case FindingSeverity::high: return "high";
  }
  return "unknown";
}

std::string finding_category_name(const FindingCategory category) {
  switch (category) {
    case FindingCategory::technical: return "technical";
    case FindingCategory::tonal: return "tonal";
    case FindingCategory::dynamics: return "dynamics";
    case FindingCategory::transient: return "transient";
    case FindingCategory::stereo: return "stereo";
    case FindingCategory::structure: return "structure";
    case FindingCategory::character: return "character";
  }
  return "unknown";
}

std::string analysis_report_to_json(const AnalysisReport& report) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(4)
         << "{\"schema_version\":" << report.schema_version
         << ",\"tempo\":{\"bpm\":" << report.structural.tempo.bpm
         << ",\"confidence\":" << report.structural.tempo.confidence
         << ",\"onset_density_per_second\":" << report.structural.tempo.onset_density_per_second << "}"
         << ",\"macro_dynamics\":{\"range_db\":"
         << report.structural.macro_dynamics.macro_dynamic_range_db
         << ",\"section_contrast_db\":" << report.structural.macro_dynamics.section_contrast_db << "}"
         << ",\"perceptual\":{\"harshness\":" << report.perceptual.harshness_score
         << ",\"mud\":" << report.perceptual.mud_score
         << ",\"sub_buildup\":" << report.perceptual.sub_buildup_score
         << ",\"brightness\":" << report.perceptual.brightness_score << "}"
         << ",\"character\":{\"hard_clip_likelihood\":" << report.character.hard_clip_likelihood
         << ",\"saturation_likelihood\":" << report.character.saturation_likelihood
         << ",\"intentional_character_likelihood\":" << report.character.intentional_character_likelihood
         << ",\"accidental_defect_risk\":" << report.character.accidental_defect_risk
         << ",\"confidence\":" << report.character.inference_confidence << "}"
         << ",\"mix_health\":{\"heuristic_score\":" << report.mix_health.overall_heuristic_score
         << ",\"confidence\":" << report.mix_health.overall_confidence
         << ",\"assessment\":\"" << escape_json(report.mix_health.overall_assessment) << "\",\"dimensions\":[";
  for (std::size_t index = 0U; index < report.mix_health.dimensions.size(); ++index) {
    if (index != 0U) stream << ',';
    const auto& dimension = report.mix_health.dimensions[index];
    stream << "{\"id\":\"" << escape_json(dimension.id) << "\",\"score\":"
           << dimension.heuristic_score << ",\"confidence\":" << dimension.confidence
           << ",\"assessment\":\"" << escape_json(dimension.assessment) << "\"}";
  }
  stream << "]},\"sections\":[";
  for (std::size_t index = 0U; index < report.structural.sections.size(); ++index) {
    if (index != 0U) stream << ',';
    const auto& section = report.structural.sections[index];
    stream << "{\"start\":" << section.start_seconds << ",\"end\":" << section.end_seconds
           << ",\"energy_dbfs\":" << section.energy_dbfs << ",\"label_hint\":\""
           << escape_json(section.label_hint) << "\",\"confidence\":" << section.confidence << "}";
  }
  stream << "],\"findings\":[";
  for (std::size_t index = 0U; index < report.findings.size(); ++index) {
    if (index != 0U) stream << ',';
    const auto& finding = report.findings[index];
    stream << "{\"id\":\"" << escape_json(finding.id) << "\",\"category\":\""
           << finding_category_name(finding.category) << "\",\"severity\":\""
           << finding_severity_name(finding.severity) << "\",\"confidence\":" << finding.confidence
           << ",\"heuristic\":" << (finding.heuristic ? "true" : "false")
           << ",\"title\":\"" << escape_json(finding.title) << "\",\"detail\":\""
           << escape_json(finding.detail) << "\"";
    if (finding.has_time_range) {
      stream << ",\"start\":" << finding.start_seconds << ",\"end\":" << finding.end_seconds;
    }
    stream << '}';
  }
  stream << "]}";
  return stream.str();
}

}  // namespace amt::analysis
