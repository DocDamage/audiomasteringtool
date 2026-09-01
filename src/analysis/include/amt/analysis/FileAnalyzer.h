#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "amt/analysis/IntegrityAnalyzer.h"
#include "amt/analysis/LoudnessMeter.h"
#include "amt/analysis/SpectrumAnalyzer.h"
#include "amt/analysis/StereoAnalyzer.h"
#include "amt/audio/WaveformCache.h"
#include "amt/codec/AudioIO.h"
#include "amt/core/JobControl.h"

namespace amt::analysis {

struct Phase1AnalysisReport {
  amt::codec::AudioMetadata metadata;
  LoudnessMetrics loudness;
  SpectrumMetrics spectrum;
  StereoMetrics stereo;
  IntegrityMetrics integrity;
  amt::audio::WaveformPeakCache waveform;
  double inter_sample_peak_delta_db{0.0};
};

[[nodiscard]] std::optional<Phase1AnalysisReport> analyze_file(
    amt::codec::ICodecService& codecs, const std::filesystem::path& path,
    std::string& error,
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::analysis
