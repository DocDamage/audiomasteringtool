#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "amt/codec/AudioIO.h"
#include "amt/separation/Separation.h"
#include "amt/separation/SourceGuidedProcessing.h"

namespace amt::separation {

struct SourceControlEnvelopeConfig {
  double hop_seconds{0.020};
  double activity_floor_db{-60.0};
  double normalization_percentile{0.95};
};

struct SourceControlEnvelope {
  StemRole source{StemRole::unknown};
  int sample_rate{0};
  std::size_t hop_frames{0U};
  double source_confidence{0.0};
  std::vector<float> activity;
};

struct SourceGuidedControlBinding {
  SourceGuidedIntervention intervention;
  std::size_t envelope_index{0U};
};

struct SourceGuidedControlPlan {
  bool operates_on_canonical_stereo{true};
  std::vector<SourceControlEnvelope> envelopes;
  std::vector<SourceGuidedControlBinding> bindings;
  std::vector<std::string> skipped_reasons;
};

[[nodiscard]] std::optional<SourceControlEnvelope> build_source_control_envelope(
    amt::codec::ICodecService& codecs,
    const SeparationArtifactReference& stem_audio,
    std::string& error,
    const SourceControlEnvelopeConfig& config = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

[[nodiscard]] std::optional<std::vector<SourceControlEnvelope>>
build_source_control_envelopes(
    amt::codec::ICodecService& codecs,
    const SeparationResult& separation,
    std::string& error,
    const SourceControlEnvelopeConfig& config = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

[[nodiscard]] SourceGuidedControlPlan bind_source_guided_controls(
    const SourceGuidedProcessingPlan& processing,
    std::vector<SourceControlEnvelope> envelopes);

[[nodiscard]] double source_activity_at_frame(
    const SourceControlEnvelope& envelope,
    std::int64_t frame) noexcept;

[[nodiscard]] double controlled_intervention_amount_at_frame(
    const SourceGuidedControlPlan& plan,
    const SourceGuidedControlBinding& binding,
    std::int64_t frame,
    int program_sample_rate) noexcept;

}  // namespace amt::separation
