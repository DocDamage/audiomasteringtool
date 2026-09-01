#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

#include "amt/analysis/DeepAnalysis.h"
#include "amt/analysis/FileAnalyzer.h"
#include "amt/codec/AudioIO.h"
#include "amt/codec/SndFileCodec.h"
#include "amt/codec/SndFileDynamic.h"
#include "amt/core/Version.h"
#include "amt/mastering/Audition.h"
#include "amt/mastering/OfflineRenderer.h"
#include "amt/mastering/Planner.h"
#include "amt/mastering/SourceGuidedCalibration.h"
#include "amt/playback/ComparisonTransport.h"
#include "amt/playback/Transport.h"

namespace {

void usage() {
  std::cout << "AudioMasteringTool Phase 5 CLI\n"
            << "  amt_cli --version\n"
            << "  amt_cli codec-status\n"
            << "  amt_cli probe <input>\n"
            << "  amt_cli analyze <input>\n"
            << "  amt_cli deep-analyze <input> [--json]\n"
            << "  amt_cli plan <input>\n"
            << "  amt_cli master <input> <output-directory> [--bits 16|24|32|float]\n"
            << "  amt_cli calibrate-source-guidance <input> <output-directory>"
               " --registry <registry.json> --worker <amt_worker> --model-root <dir>"
               " [--bits 16|24|32|float]\n"
            << "  amt_cli audition <original> <master-a> <master-b>\n"
            << "  amt_cli export <input> <output> [--sample-rate N] [--bits 16|24|32|float]\n"
            << "  amt_cli compare <first> <second> [--tolerance value]\n"
            << "  amt_cli play <input>\n"
            << "  amt_cli rerender <input> <output>   # Phase 0 bit-exact compatibility\n"
            << "  amt_cli verify <first> <second>     # Phase 0 decoded-PCM comparison\n";
}

std::optional<amt::codec::AudioSampleFormat> parse_bits(const std::string& value) {
  if (value == "16") return amt::codec::AudioSampleFormat::pcm16;
  if (value == "24") return amt::codec::AudioSampleFormat::pcm24;
  if (value == "32") return amt::codec::AudioSampleFormat::pcm32;
  if (value == "float") return amt::codec::AudioSampleFormat::float32;
  return std::nullopt;
}

void print_probe(const amt::codec::AudioMetadata& info) {
  std::cout << "container=" << info.container_name << " format=" << info.sample_format_name
            << " rate=" << info.sample_rate << " channels=" << info.channels
            << " frames=" << info.frames << " bits=" << info.bit_depth
            << " seekable=" << (info.seekable ? "yes" : "no") << '\n';
}

void print_candidate(const amt::mastering::MasteringCandidatePlan& candidate) {
  std::cout << candidate.name << " (" << candidate.id << ")"
            << (candidate.recommended ? " [Recommended]" : "") << '\n'
            << "  target_lufs=" << candidate.target_lufs
            << " ceiling_dbtp=" << candidate.ceiling_dbtp
            << " preservation_bias=" << candidate.preservation_bias << '\n';
  for (const auto& reason : candidate.rationale) std::cout << "  - " << reason << '\n';
  std::cout << "  graph=" << candidate.graph.to_json() << '\n';
}

void print_analysis(const amt::analysis::Phase1AnalysisReport& report) {
  print_probe(report.metadata);
  std::cout << std::fixed << std::setprecision(3)
            << "integrated_lufs=" << report.loudness.integrated_lufs << '\n'
            << "momentary_max_lufs=" << report.loudness.max_momentary_lufs << '\n'
            << "short_term_max_lufs=" << report.loudness.max_short_term_lufs << '\n'
            << "lra_lu=" << report.loudness.loudness_range_lu << '\n'
            << "sample_peak_dbfs=" << report.loudness.sample_peak_dbfs << '\n'
            << "true_peak_dbtp=" << report.loudness.true_peak_dbtp << '\n'
            << "crest_factor_db=" << report.loudness.crest_factor_db << '\n'
            << "plr_db=" << report.loudness.peak_to_loudness_ratio_db << '\n'
            << "spectral_centroid_hz=" << report.spectrum.centroid_hz << '\n'
            << "spectral_rolloff85_hz=" << report.spectrum.rolloff_85_hz << '\n'
            << "stereo_correlation=" << report.stereo.correlation << '\n'
            << "low_width=" << report.stereo.low_band_width << '\n'
            << "mid_width=" << report.stereo.mid_band_width << '\n'
            << "high_width=" << report.stereo.high_band_width << '\n'
            << "mono_delta_db=" << report.stereo.mono_fold_down_delta_db << '\n'
            << "clipped_samples=" << report.integrity.clipped_samples << '\n'
            << "nan_samples=" << report.integrity.nan_samples << '\n'
            << "infinite_samples=" << report.integrity.infinite_samples << '\n'
            << "dc_offset=" << report.integrity.max_absolute_dc_offset << '\n'
            << "waveform_levels=" << report.waveform.levels.size() << '\n';
}

void print_deep_analysis(const amt::analysis::AnalysisReport& report) {
  print_analysis(report.technical);
  std::cout << std::fixed << std::setprecision(3)
            << "\nSTRUCTURE\n"
            << "tempo_bpm=" << report.structural.tempo.bpm
            << " confidence=" << report.structural.tempo.confidence << '\n'
            << "onset_density_per_second=" << report.structural.tempo.onset_density_per_second << '\n'
            << "macro_dynamic_range_db=" << report.structural.macro_dynamics.macro_dynamic_range_db << '\n'
            << "section_contrast_db=" << report.structural.macro_dynamics.section_contrast_db << '\n'
            << "sections=" << report.structural.sections.size() << '\n';
  for (std::size_t index = 0; index < report.structural.sections.size(); ++index) {
    const auto& section = report.structural.sections[index];
    std::cout << "  [" << index << "] " << section.start_seconds << "-" << section.end_seconds
              << "s " << section.label_hint << " energy=" << section.energy_dbfs
              << " dBFS transient_density=" << section.transient_density
              << " width=" << section.stereo_width << " confidence=" << section.confidence << '\n';
  }

  std::cout << "\nPERCEPTUAL HEURISTICS\n"
            << "harshness=" << report.perceptual.harshness_score
            << " mud=" << report.perceptual.mud_score
            << " sub_buildup=" << report.perceptual.sub_buildup_score
            << " brightness=" << report.perceptual.brightness_score
            << " tonal_imbalance=" << report.perceptual.tonal_imbalance_score << '\n';
  for (const auto& resonance : report.perceptual.resonances) {
    std::cout << "  resonance " << resonance.frequency_hz << " Hz prominence="
              << resonance.prominence_db << " dB persistence=" << resonance.persistence
              << " severity=" << resonance.severity;
    if (resonance.last_seen_seconds > resonance.first_seen_seconds) {
      std::cout << " range=" << resonance.first_seen_seconds << "-" << resonance.last_seen_seconds << "s";
    }
    std::cout << '\n';
  }

  std::cout << "\nCHARACTER / DEFECT HEURISTICS\n"
            << "hard_clip_likelihood=" << report.character.hard_clip_likelihood << '\n'
            << "saturation_likelihood=" << report.character.saturation_likelihood << '\n'
            << "intentional_character_likelihood=" << report.character.intentional_character_likelihood << '\n'
            << "accidental_defect_risk=" << report.character.accidental_defect_risk << '\n'
            << "inference_confidence=" << report.character.inference_confidence << '\n';

  std::cout << "\nMIX HEALTH V1 — heuristic assessment, not an objective quality score\n"
            << "overall=" << report.mix_health.overall_heuristic_score
            << " confidence=" << report.mix_health.overall_confidence
            << " assessment=" << report.mix_health.overall_assessment << '\n';
  for (const auto& dimension : report.mix_health.dimensions) {
    std::cout << "  " << dimension.label << ": " << dimension.heuristic_score
              << " confidence=" << dimension.confidence
              << " assessment=" << dimension.assessment << '\n';
  }

  std::cout << "\nFINDINGS\n";
  for (const auto& finding : report.findings) {
    std::cout << "[" << amt::analysis::finding_severity_name(finding.severity) << "] ["
              << amt::analysis::finding_category_name(finding.category) << "] " << finding.title
              << " — confidence " << finding.confidence
              << (finding.heuristic ? " (heuristic)" : " (measurement)") << '\n'
              << "  " << finding.detail << '\n';
    if (finding.has_time_range) {
      std::cout << "  range: " << finding.start_seconds << "-" << finding.end_seconds << " s\n";
    }
    for (const auto& evidence : finding.evidence) std::cout << "  evidence: " << evidence << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }

  const std::string command = argv[1];
  if (command == "--version") {
    std::cout << amt::core::version() << '\n';
    return 0;
  }

  amt::codec::SndFileCodecService codecs;
  if (command == "codec-status") {
    if (!codecs.available()) {
      std::cerr << "unavailable: " << codecs.backend_error() << '\n';
      return 1;
    }
    std::cout << codecs.backend_name() << '\n';
    for (const auto& capability : codecs.capabilities()) {
      std::cout << "  " << capability.name << " decode=" << capability.decode
                << " encode=" << capability.encode << " lossless=" << capability.lossless << '\n';
    }
    return 0;
  }

  if (!codecs.available()) {
    std::cerr << "codec error: " << codecs.backend_error() << '\n';
    return 1;
  }

  std::string error;
  if (command == "probe" && argc == 3) {
    const auto info = codecs.probe(argv[2], error);
    if (!info) {
      std::cerr << "probe failed: " << error << '\n';
      return 1;
    }
    print_probe(*info);
    return 0;
  }

  if (command == "analyze" && argc == 3) {
    const auto report = amt::analysis::analyze_file(codecs, argv[2], error);
    if (!report) {
      std::cerr << "analysis failed: " << error << '\n';
      return 1;
    }
    print_analysis(*report);
    return 0;
  }

  if (command == "deep-analyze" && (argc == 3 || argc == 4)) {
    if (argc == 4 && std::string(argv[3]) != "--json") {
      usage();
      return 2;
    }
    const auto report = amt::analysis::analyze_track(codecs, argv[2], error);
    if (!report) {
      std::cerr << "deep analysis failed: " << error << '\n';
      return 1;
    }
    if (argc == 4) std::cout << amt::analysis::analysis_report_to_json(*report) << '\n';
    else print_deep_analysis(*report);
    return 0;
  }

  if (command == "plan" && argc == 3) {
    const auto report = amt::analysis::analyze_file(codecs, argv[2], error);
    if (!report) {
      std::cerr << "analysis failed: " << error << '\n';
      return 1;
    }
    const auto plan = amt::mastering::plan_mastering(*report);
    print_candidate(plan.master_a);
    print_candidate(plan.master_b);
    return 0;
  }

  if (command == "master" && argc >= 4) {
    amt::mastering::RenderSettings settings;
    for (int index = 4; index < argc; ++index) {
      const std::string option = argv[index];
      if (option == "--bits" && index + 1 < argc) {
        const auto format = parse_bits(argv[++index]);
        if (!format) {
          std::cerr << "invalid --bits value\n";
          return 2;
        }
        settings.sample_format = *format;
      } else {
        std::cerr << "unknown master option: " << option << '\n';
        return 2;
      }
    }
    const auto report = amt::analysis::analyze_file(codecs, argv[2], error);
    if (!report) {
      std::cerr << "analysis failed: " << error << '\n';
      return 1;
    }
    const auto plan = amt::mastering::plan_mastering(*report);
    std::cout << "Generating two deterministic masters...\n";
    const auto rendered = amt::mastering::render_mastering_plan(
        codecs, argv[2], argv[3], *report, plan, error, settings);
    if (!rendered) {
      std::cerr << "mastering failed: " << error << '\n';
      return 1;
    }
    std::cout << std::fixed << std::setprecision(3)
              << "Recommended: Master A\n"
              << "Master A: " << rendered->master_a.output_path.string()
              << " LUFS=" << rendered->master_a.analysis.loudness.integrated_lufs
              << " dBTP=" << rendered->master_a.analysis.loudness.true_peak_dbtp << '\n'
              << "Master B: " << rendered->master_b.output_path.string()
              << " LUFS=" << rendered->master_b.analysis.loudness.integrated_lufs
              << " dBTP=" << rendered->master_b.analysis.loudness.true_peak_dbtp << '\n'
              << "Loudness-matched audition reference=" << rendered->audition.reference_lufs << " LUFS\n"
              << "  Original audition gain=" << rendered->audition.original_gain_db << " dB\n"
              << "  Master A audition gain=" << rendered->audition.master_a_gain_db << " dB\n"
              << "  Master B audition gain=" << rendered->audition.master_b_gain_db << " dB\n";
    return 0;
  }

  if (command == "calibrate-source-guidance" && argc >= 10) {
    amt::mastering::SourceGuidedCalibrationRequest request;
    request.source_path = argv[2];
    request.output_directory = argv[3];

    for (int index = 4; index < argc; ++index) {
      const std::string option = argv[index];
      if (option == "--registry" && index + 1 < argc) {
        request.registry_path = argv[++index];
      } else if (option == "--worker" && index + 1 < argc) {
        request.worker_executable = argv[++index];
      } else if (option == "--model-root" && index + 1 < argc) {
        request.model_store_root = argv[++index];
      } else if (option == "--bits" && index + 1 < argc) {
        const auto format = parse_bits(argv[++index]);
        if (!format) {
          std::cerr << "invalid --bits value\n";
          return 2;
        }
        request.render_settings.sample_format = *format;
      } else {
        std::cerr << "unknown calibration option: " << option << '\n';
        return 2;
      }
    }

    if (request.registry_path.empty() || request.worker_executable.empty() ||
        request.model_store_root.empty()) {
      std::cerr << "calibration requires --registry, --worker, and --model-root\n";
      return 2;
    }

    std::cout << "Generating stereo/source-guided calibration candidates...\n";
    const auto result = amt::mastering::render_source_guided_calibration_pair(
        codecs, request, error, nullptr,
        [](const double value) {
          const int percent = static_cast<int>(std::lround(
              std::clamp(value, 0.0, 1.0) * 100.0));
          std::cout << "\rCalibration " << std::setw(3) << percent << "%" << std::flush;
        });
    std::cout << '\n';
    if (!result) {
      std::cerr << "calibration render failed: " << error << '\n';
      return 1;
    }

    std::cout << "Evidence mode: "
              << amt::separation::separation_mode_name(result->evidence_mode) << '\n'
              << "Source estimates analyzed: "
              << (result->source_estimates_analyzed ? "yes" : "no") << '\n'
              << "Stereo Master A: " << result->stereo_master_a.string() << '\n'
              << "Guided candidate rendered: "
              << (result->guided_candidate_rendered ? "yes" : "no") << '\n';
    if (result->guided_candidate_rendered) {
      std::cout << "Guided Master A: " << result->guided_master_a.string() << '\n';
    }
    std::cout << "Calibration manifest: " << result->manifest_path.string() << '\n';
    for (const auto& issue : result->issues) {
      std::cout << "  source issue: " << amt::separation::stem_role_name(issue.source)
                << ' ' << amt::separation::source_guided_issue_name(issue.type)
                << " severity=" << issue.severity
                << " confidence=" << issue.confidence << '\n';
    }
    for (const auto& warning : result->warnings) {
      std::cout << "  warning: " << warning << '\n';
    }
    return 0;
  }

  if (command == "audition" && argc == 5) {
    const auto original = amt::analysis::analyze_file(codecs, argv[2], error);
    if (!original) {
      std::cerr << "original analysis failed: " << error << '\n';
      return 1;
    }
    const auto a = amt::analysis::analyze_file(codecs, argv[3], error);
    if (!a) {
      std::cerr << "Master A analysis failed: " << error << '\n';
      return 1;
    }
    const auto b = amt::analysis::analyze_file(codecs, argv[4], error);
    if (!b) {
      std::cerr << "Master B analysis failed: " << error << '\n';
      return 1;
    }
    const auto match = amt::mastering::make_loudness_match_profile(
        original->loudness, a->loudness, b->loudness);
    amt::playback::ComparisonTransport transport(codecs);
    if (!transport.load(
            {.path = argv[2], .audition_gain_db = match.original_gain_db},
            {.path = argv[3], .audition_gain_db = match.master_a_gain_db},
            {.path = argv[4], .audition_gain_db = match.master_b_gain_db}, error) ||
        !transport.play(error)) {
      std::cerr << "audition failed: " << error << '\n';
      return 1;
    }
    std::cout << "Loudness-matched synchronized audition via " << transport.output_backend_name() << '\n'
              << "Commands: o=Original, a=Master A, b=Master B, p=pause/resume, s <seconds>=seek, q=quit\n";
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line == "q") break;
      if (line == "o") transport.select(amt::playback::ComparisonSource::original);
      else if (line == "a") transport.select(amt::playback::ComparisonSource::master_a);
      else if (line == "b") transport.select(amt::playback::ComparisonSource::master_b);
      else if (line == "p") {
        if (transport.state() == amt::playback::TransportState::playing) {
          if (!transport.pause(error)) std::cerr << error << '\n';
        } else if (transport.state() == amt::playback::TransportState::paused) {
          if (!transport.resume(error)) std::cerr << error << '\n';
        }
      } else if (line.rfind("s ", 0U) == 0U) {
        try {
          const double seconds = std::stod(line.substr(2U));
          const auto* metadata = transport.metadata();
          if (metadata != nullptr) {
            const double duration = metadata->frames / static_cast<double>(metadata->sample_rate);
            const auto frame = static_cast<std::int64_t>(
                std::clamp(seconds, 0.0, duration) * metadata->sample_rate);
            if (!transport.seek(frame, error)) std::cerr << error << '\n';
          }
        } catch (...) {
          std::cerr << "invalid seek time\n";
        }
      }
      if (transport.state() == amt::playback::TransportState::finished) break;
    }
    transport.stop();
    return 0;
  }

  if (command == "export" && argc >= 4) {
    amt::codec::ExportRequest request;
    for (int index = 4; index < argc; ++index) {
      const std::string option = argv[index];
      if (option == "--sample-rate" && index + 1 < argc) {
        try {
          request.sample_rate = std::stoi(argv[++index]);
        } catch (...) {
          std::cerr << "invalid --sample-rate\n";
          return 2;
        }
      } else if (option == "--bits" && index + 1 < argc) {
        const auto format = parse_bits(argv[++index]);
        if (!format) {
          std::cerr << "invalid --bits value\n";
          return 2;
        }
        request.sample_format = *format;
      } else {
        std::cerr << "unknown export option: " << option << '\n';
        return 2;
      }
    }
    if (!amt::codec::export_audio(codecs, argv[2], argv[3], request, error)) {
      std::cerr << "export failed: " << error << '\n';
      return 1;
    }
    std::cout << "export: ok\n";
    return 0;
  }

  if (command == "compare" && argc >= 4) {
    double tolerance = 1.0e-7;
    if (argc == 6 && std::string(argv[4]) == "--tolerance") {
      try {
        tolerance = std::stod(argv[5]);
      } catch (...) {
        std::cerr << "invalid comparison tolerance\n";
        return 2;
      }
    } else if (argc != 4) {
      usage();
      return 2;
    }
    if (!amt::codec::verify_audio_equal(codecs, argv[2], argv[3], tolerance, error)) {
      std::cerr << "compare failed: " << error << '\n';
      return 1;
    }
    std::cout << "compare: decoded audio matches within tolerance " << tolerance << '\n';
    return 0;
  }

  if (command == "play" && argc == 3) {
    amt::playback::Transport transport(codecs);
    if (!transport.load(argv[2], error) || !transport.play(error)) {
      std::cerr << "playback failed: " << error << '\n';
      return 1;
    }
    std::cout << "playing via " << transport.output_backend_name() << '\n';
    transport.wait_until_finished();
    return 0;
  }

  amt::codec::SndFileRuntime legacy_runtime;
  if (command == "rerender" && argc == 4) {
    if (!legacy_runtime.lossless_rerender(argv[2], argv[3], error)) {
      std::cerr << "rerender failed: " << error << '\n';
      return 1;
    }
    std::cout << "rerender: ok\n";
    return 0;
  }
  if (command == "verify" && argc == 4) {
    if (!legacy_runtime.verify_pcm_equal(argv[2], argv[3], error)) {
      std::cerr << "verify failed: " << error << '\n';
      return 1;
    }
    std::cout << "verify: decoded PCM is lossless\n";
    return 0;
  }

  usage();
  return 2;
}
