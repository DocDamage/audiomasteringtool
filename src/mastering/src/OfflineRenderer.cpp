#include "amt/mastering/OfflineRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "amt/mastering/ProcessingGraph.h"

namespace amt::mastering {
namespace {

constexpr std::size_t kRenderFrames = 8192U;
constexpr std::size_t kMaxRenderNameAttempts = 100000U;

double db_to_linear(const double db) { return std::pow(10.0, db / 20.0); }

std::filesystem::path append_ascii(std::filesystem::path value, const std::string& suffix) {
  value += suffix;
  return value;
}

std::filesystem::path temp_path_for(const std::filesystem::path& output,
                                    const std::string& suffix) {
  auto filename = output.stem();
  filename = append_ascii(std::move(filename), suffix);
  filename = append_ascii(std::move(filename), ".wav");
  return output.parent_path() / filename;
}

void remove_if_exists(const std::filesystem::path& path) {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

std::optional<std::pair<std::filesystem::path, std::filesystem::path>>
allocate_render_paths(const std::filesystem::path& output_directory,
                      const std::filesystem::path& stem, std::string& error) {
  for (std::size_t attempt = 1U; attempt <= kMaxRenderNameAttempts; ++attempt) {
    const std::string revision_suffix = attempt == 1U
        ? std::string{}
        : "_r" + std::to_string(attempt);

    auto filename_a = append_ascii(stem, "_Master_A");
    filename_a = append_ascii(std::move(filename_a), revision_suffix);
    filename_a = append_ascii(std::move(filename_a), ".wav");
    auto filename_b = append_ascii(stem, "_Master_B");
    filename_b = append_ascii(std::move(filename_b), revision_suffix);
    filename_b = append_ascii(std::move(filename_b), ".wav");
    const auto output_a = output_directory / filename_a;
    const auto output_b = output_directory / filename_b;

    std::error_code exists_error;
    const bool a_exists = std::filesystem::exists(output_a, exists_error);
    if (exists_error) {
      error = "unable to inspect Master A render path: " + exists_error.message();
      return std::nullopt;
    }
    const bool b_exists = std::filesystem::exists(output_b, exists_error);
    if (exists_error) {
      error = "unable to inspect Master B render path: " + exists_error.message();
      return std::nullopt;
    }
    if (!a_exists && !b_exists) return std::pair{output_a, output_b};
  }

  error = "unable to allocate a unique mastering render filename";
  return std::nullopt;
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
  const auto temp_safety = temp_path_for(output, ".amt-safety");
  remove_if_exists(temp_render);
  remove_if_exists(temp_adjusted);
  remove_if_exists(temp_safety);

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
  double correction = std::clamp(std::min(loudness_correction, peak_headroom), -12.0, 3.0);
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
                                  amt::core::report_progress(progress, 0.68 + value * 0.18);
                                })) {
    remove_if_exists(temp_render);
    remove_if_exists(temp_adjusted);
    remove_if_exists(output);
    return std::nullopt;
  }

  auto final_analysis = amt::analysis::analyze_file(codecs, output, error, cancellation);
  if (!final_analysis) {
    remove_if_exists(temp_render);
    remove_if_exists(temp_adjusted);
    remove_if_exists(output);
    return std::nullopt;
  }

  if (final_analysis->loudness.true_peak_dbtp > candidate.ceiling_dbtp + 0.02) {
    const double safety_gain = candidate.ceiling_dbtp - final_analysis->loudness.true_peak_dbtp - 0.03;
    correction += safety_gain;
    if (!apply_gain_to_float(codecs, output, temp_safety, safety_gain, error, cancellation) ||
        !amt::codec::export_audio(codecs, temp_safety, output, export_request, error, cancellation)) {
      remove_if_exists(temp_render);
      remove_if_exists(temp_adjusted);
      remove_if_exists(temp_safety);
      remove_if_exists(output);
      return std::nullopt;
    }
    final_analysis = amt::analysis::analyze_file(codecs, output, error, cancellation);
    if (!final_analysis) {
      remove_if_exists(temp_render);
      remove_if_exists(temp_adjusted);
      remove_if_exists(temp_safety);
      remove_if_exists(output);
      return std::nullopt;
    }
  }

  remove_if_exists(temp_render);
  remove_if_exists(temp_adjusted);
  remove_if_exists(temp_safety);
  amt::core::report_progress(progress, 1.0);

  RenderResult result;
  result.output_path = output;
  result.analysis = *final_analysis;
  result.final_gain_correction_db = correction;
  result.loudness_error_lu = final_analysis->loudness.integrated_lufs - candidate.target_lufs;
  result.peak_ceiling_met = final_analysis->loudness.true_peak_dbtp <= candidate.ceiling_dbtp + 0.05;
  if (settings.verify_output && !result.peak_ceiling_met) {
    remove_if_exists(output);
    error = "render exceeded true-peak ceiling after safety correction";
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

  const auto allocated = allocate_render_paths(output_directory, input.stem(), error);
  if (!allocated) return std::nullopt;
  const auto& [output_a, output_b] = *allocated;

  const auto a = render_candidate(codecs, input, output_a, plan.master_a, error, settings,
                                  cancellation, [&](const double value) {
                                    amt::core::report_progress(progress, value * 0.5);
                                  });
  if (!a) return std::nullopt;
  const auto b = render_candidate(codecs, input, output_b, plan.master_b, error, settings,
                                  cancellation, [&](const double value) {
                                    amt::core::report_progress(progress, 0.5 + value * 0.5);
                                  });
  if (!b) {
    remove_if_exists(a->output_path);
    return std::nullopt;
  }

  return MasteringRenderPair{.master_a = *a,
                             .master_b = *b,
                             .audition = make_loudness_match_profile(
                                 source_analysis.loudness, a->analysis.loudness,
                                 b->analysis.loudness)};
}

}  // namespace amt::mastering
