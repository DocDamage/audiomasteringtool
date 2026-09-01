#include "amt/analysis/FileAnalyzer.h"

#include <algorithm>
#include <cstdint>
#include <exception>

namespace amt::analysis {

std::optional<Phase1AnalysisReport> analyze_file(
    amt::codec::ICodecService& codecs, const std::filesystem::path& path,
    std::string& error, const amt::core::CancellationToken* cancellation,
    const amt::core::ProgressCallback& progress) {
  auto decoder = codecs.open_decoder(path, error);
  if (!decoder) return std::nullopt;
  const auto metadata = decoder->metadata();
  if (metadata.sample_rate <= 0 || metadata.channels <= 0) {
    error = "invalid source metadata";
    return std::nullopt;
  }

  try {
    LoudnessMeter loudness(metadata.sample_rate, static_cast<std::size_t>(metadata.channels));
    SpectrumAnalyzer spectrum(metadata.sample_rate, static_cast<std::size_t>(metadata.channels));
    StereoAnalyzer stereo(metadata.sample_rate, static_cast<std::size_t>(metadata.channels));
    IntegrityAnalyzer integrity(metadata.sample_rate, static_cast<std::size_t>(metadata.channels));
    amt::audio::WaveformPeakAccumulator waveform(
        metadata.sample_rate, static_cast<std::size_t>(metadata.channels));

    std::int64_t consumed = 0;
    while (true) {
      if (cancellation != nullptr && cancellation->is_cancelled()) {
        error = "analysis cancelled";
        return std::nullopt;
      }
      amt::audio::AudioBuffer buffer;
      std::size_t frames_read = 0U;
      if (!decoder->read(buffer, 8192U, frames_read, error, cancellation)) return std::nullopt;
      if (frames_read == 0U) break;
      loudness.process(buffer);
      spectrum.process(buffer);
      stereo.process(buffer);
      integrity.process(buffer);
      waveform.append(buffer);
      consumed += static_cast<std::int64_t>(frames_read);
      if (metadata.frames > 0) {
        amt::core::report_progress(progress, static_cast<double>(consumed) /
                                                 static_cast<double>(metadata.frames));
      }
    }

    Phase1AnalysisReport report;
    report.metadata = metadata;
    report.loudness = loudness.finalize();
    report.spectrum = spectrum.finalize();
    report.stereo = stereo.finalize();
    report.integrity = integrity.finalize();
    report.waveform = waveform.finalize();
    report.inter_sample_peak_delta_db =
        std::max(0.0, report.loudness.true_peak_dbtp - report.loudness.sample_peak_dbfs);
    amt::core::report_progress(progress, 1.0);
    return report;
  } catch (const std::exception& exception) {
    error = exception.what();
    return std::nullopt;
  }
}

}  // namespace amt::analysis
