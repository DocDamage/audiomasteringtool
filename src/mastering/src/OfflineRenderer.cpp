#include "amt/mastering/OfflineRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "amt/mastering/ProcessingGraph.h"

namespace amt::mastering {
namespace {

constexpr std::size_t kRenderFrames = 8192U;

double db_to_linear(const double db) { return std::pow(10.0, db / 20.0); }

std::filesystem::path temp_path_for(const std::filesystem::path& output, const char* suffix) {
  auto path = output;
  path += suffix;
  path.replace_extension(".wav");
  return path;
}

void remove_if_exists(const std::filesystem::path& path) {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

bool render_graph_to_float(
    amt::codec::ICodecService& codecs, const std::filesystem::path& input,
    const std::filesystem::path& output, const ProcessingGraph& graph,
    std::string& error, const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  auto decoder = codecs.open_decoder(input, error);
  if (!decoder) return false;
  const auto metadata = decoder->metadata();
  amt::codec::EncodeSettings settings;
  settings.sample_rate = metadata.sample_rate;
  settings.channels = metadata.channels;
  settings.container = amt::codec::AudioContainer::wav;
  settings.sample_format = amt::codec::AudioSampleFormat::float32;
  settings.tags = metadata.tags;
  auto encoder = codecs.open_encoder(output, settings, error);
  if (!encoder) return false;

  ProcessingGraphRuntime runtime(graph, metadata.sample_rate,
                                 static_cast<std::size_t>(metadata.channels));
  std::int64_t consumed = 0;
  while (true) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "mastering render cancelled";
      return false;
    }
    amt::audio::AudioBuffer buffer;
    std::size_t frames = 0U;
    if (!decoder->read(buffer, kRenderFrames, frames, error, cancellation)) return false;
    if (frames == 0U) break;
    runtime.process(buffer);
    if (!encoder->write(buffer, error, cancellation)) return false;
    consumed += static_cast<std::int64_t>(frames);
    if (metadata.frames > 0) {
      amt::core::report_progress(progress, 0.48 * static_cast<double>(consumed) /
                                               static_cast<double>(metadata.frames));
    }
  }
  return encoder->finalize(error);
}

bool apply_gain_to_float(
    amt::codec::ICodecService& codecs, const std::filesystem::path& input,
    const std::filesystem::path& output, const double gain_db, std::string& error,
    const amt::core::CancellationToken* cancellation) {
  auto decoder = codecs.open_decoder(input, error);
  if (!decoder) return false;
  const auto metadata = decoder->metadata();
  amt::codec::EncodeSettings settings;
  settings.sample_rate = metadata.sample_rate;
  settings.channels = metadata.channels;
  settings.container = amt::codec::AudioContainer::wav;
  settings.sample_format = amt::codec::AudioSampleFormat::float32;
  settings.tags = metadata.tags;
  auto encoder = codecs.open_encoder(output, settings, error);
  if (!encoder) return false;
  const float gain = static_cast<float>(db_to_linear(gain_db));
  while (true) {
    amt::audio::AudioBuffer buffer;
    std::size_t frames = 0U;
    if (!decoder->read(buffer, kRenderFrames, frames, error, cancellation)) return false;
    if (frames == 0U) break;
    for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
      for (float& sample : buffer.channel(channel)) sample *= gain;
    }
    if (!encoder->write(buffer, error, cancellation)) return false;
  }
  return encoder->finalize(error);
}

}  // namespace

std::optional<RenderResult> render_candidate(
    amt::codec::ICodecService& codecs, const std::filesystem::path& input,
    const std::filesystem::path& output, const MasteringCandidatePlan& candidate,
    std::string& error, const RenderSettings& settings,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  std::string graph_error;
  if (!candidate.graph.validate(graph_error)) {
    error = "invalid mastering graph: " + graph_error;
    return std::nullopt;
  }

  const auto temp_render = temp_path_for(output, ".amt-render");
  const auto temp_adjusted = temp_path_for(output, ".amt-adjusted");
  remove_if_exists(temp_render);
  remove_if_exists(temp_adjusted);

  if (!render_graph_to_float(codecs, input, temp_render, candidate.graph, error,
                             cancellation, progress)) {
    remove_if_exists(temp_render);
    return std::nullopt;
  }

  const auto preliminary = amt::analysis::analyze_file(codecs, temp_render, error, cancellation);
  if (!preliminary) {
    remove_if_exists(temp_render);
    return std::nullopt;
  }
  amt::core::report_progress(progress, 0.58);

  const double loudness_correction = candidate.target_lufs - preliminary->loudness.integrated_lufs;
  const double peak_headroom = candidate.ceiling_dbtp - preliminary->loudness.true_peak_dbtp;
  const double correction = std::clamp(std::min(loudness_correction, peak_headroom), -6.0, 3.0);
  std::filesystem::path export_source = temp_render;
  if (std::abs(correction) > 0.025) {
    if (!apply_gain_to_float(codecs, temp_render, temp_adjusted, correction, error, cancellation)) {
      remove_if_exists(temp_render);
      remove_if_exists(temp_adjusted);
      return std::nullopt;
    }
    export_source = temp_adjusted;
  }
  amt::core::report_progress(progress, 0.68);

  amt::codec::ExportRequest export_request;
  export_request.sample_format = settings.sample_format;
  if (!amt::codec::export_audio(codecs, export_source, output, export_request, error,
                                cancellation, [&](const double value) {
                                  amt::core::report_progress(progress, 0.68 + value * 0.20);
                                })) {
    remove_if_exists(temp_render);
    remove_if_exists(temp_adjusted);
    return std::nullopt;
  }

  const auto final_analysis = amt::analysis::analyze_file(codecs, output, error, cancellation);
  remove_if_exists(temp_render);
  remove_if_exists(temp_adjusted);
  if (!final_analysis) return std::nullopt;
  amt::core::report_progress(progress, 1.0);

  RenderResult result;
  result.output_path = output;
  result.analysis = *final_analysis;
  result.final_gain_correction_db = correction;
  result.loudness_error_lu = final_analysis->loudness.integrated_lufs - candidate.target_lufs;
  result.peak_ceiling_met = final_analysis->loudness.true_peak_dbtp <= candidate.ceiling_dbtp + 0.10;
  if (settings.verify_output && !result.peak_ceiling_met) {
    error = "render exceeded true-peak ceiling after final encode";
    return std::nullopt;
  }
  return result;
}

std::optional<MasteringRenderPair> render_mastering_plan(
    amt::codec::ICodecService& codecs, const std::filesystem::path& input,
    const std::filesystem::path& output_directory,
    const amt::analysis::Phase1AnalysisReport& source_analysis, const MasteringPlan& plan,
    std::string& error, const RenderSettings& settings,
    const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  std::error_code directory_error;
  std::filesystem::create_directories(output_directory, directory_error);
  if (directory_error) {
    error = "unable to create mastering output directory";
    return std::nullopt;
  }
  const std::string stem = input.stem().string();
  const auto output_a = output_directory / (stem + "_Master_A.wav");
  const auto output_b = output_directory / (stem + "_Master_B.wav");

  const auto a = render_candidate(codecs, input, output_a, plan.master_a, error, settings,
                                  cancellation, [&](const double value) {
                                    amt::core::report_progress(progress, value * 0.5);
                                  });
  if (!a) return std::nullopt;
  const auto b = render_candidate(codecs, input, output_b, plan.master_b, error, settings,
                                  cancellation, [&](const double value) {
                                    amt::core::report_progress(progress, 0.5 + value * 0.5);
                                  });
  if (!b) return std::nullopt;

  MasteringRenderPair pair{.master_a = *a,
                           .master_b = *b,
                           .audition = make_loudness_match_profile(
                               source_analysis.loudness, a->analysis.loudness, b->analysis.loudness)};
  return pair;
}

}  // namespace amt::mastering
