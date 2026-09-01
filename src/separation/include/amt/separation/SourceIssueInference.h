#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "amt/analysis/FileAnalyzer.h"
#include "amt/codec/AudioIO.h"
#include "amt/separation/Separation.h"
#include "amt/separation/SourceGuidedProcessing.h"

namespace amt::separation {

struct SourceIssueInferenceConfig {
  double minimum_stem_confidence{0.70};
  double minimum_issue_confidence{0.65};
  double minimum_duration_seconds{2.0};
  double minimum_source_loudness_lufs{-55.0};
  double maximum_duration_mismatch_seconds{0.25};

  // Conservative global source-estimate thresholds. These do not infer an
  // instrument from the stereo mix; they are evaluated on validated stem audio.
  double excessive_level_power_share{0.86};
  double harsh_presence_energy_ratio{0.42};
  double harsh_percussive_energy_ratio{0.50};
  double muddiness_energy_ratio{0.34};
  double bass_muddiness_energy_ratio{0.30};
  double bass_low_band_width{0.16};
  double general_mid_band_width{0.42};
  double transient_crest_factor_db{13.5};
  double transient_peak_floor_dbfs{-6.0};

  // Pairwise low-frequency masking requires evidence from both separated
  // estimates and measured temporal overlap.
  double masking_bass_low_energy_ratio{0.55};
  double masking_percussion_low_energy_ratio{0.18};
  double masking_minimum_activity_overlap{0.40};

  std::size_t maximum_issues_per_source{3U};
};

struct SourceIssueInferenceResult {
  std::vector<SourceGuidedIssue> issues;
  SourceInterventionEvidence evidence;
  double measurement_confidence{0.0};
  std::vector<std::string> warnings;
};

// Derives source-specific issues only from validated separated stem audio. The
// canonical program analysis is used for relative measurements and timing, not
// to assign a problem to a source by itself.
[[nodiscard]] std::optional<SourceIssueInferenceResult> infer_source_guided_issues(
    amt::codec::ICodecService& codecs,
    const amt::analysis::Phase1AnalysisReport& canonical_analysis,
    const SeparationResult& separation,
    std::string& error,
    const SourceIssueInferenceConfig& config = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::separation
