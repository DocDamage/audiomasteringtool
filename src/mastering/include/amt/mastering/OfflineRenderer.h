#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "amt/analysis/FileAnalyzer.h"
#include "amt/codec/AudioIO.h"
#include "amt/core/JobControl.h"
#include "amt/mastering/Audition.h"
#include "amt/mastering/Planner.h"

namespace amt::mastering {

struct RenderSettings {
  amt::codec::AudioSampleFormat sample_format{amt::codec::AudioSampleFormat::pcm24};
  bool verify_output{true};
};

struct RenderResult {
  std::filesystem::path output_path;
  amt::analysis::Phase1AnalysisReport analysis;
  double final_gain_correction_db{0.0};
  double loudness_error_lu{0.0};
  bool peak_ceiling_met{false};
};

struct MasteringRenderPair {
  RenderResult master_a;
  RenderResult master_b;
  LoudnessMatchProfile audition;
};

[[nodiscard]] std::optional<RenderResult> render_candidate(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    const MasteringCandidatePlan& candidate,
    std::string& error,
    const RenderSettings& settings = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

[[nodiscard]] std::optional<MasteringRenderPair> render_mastering_plan(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis,
    const MasteringPlan& plan,
    std::string& error,
    const RenderSettings& settings = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::mastering
