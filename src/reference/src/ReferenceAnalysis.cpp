#include "amt/reference/ReferenceAnalysis.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace amt::reference {

namespace {

double compute_band_energy(
    const float* mono,
    std::size_t count,
    int sample_rate,
    double f_low,
    double f_high) {
  if (mono == nullptr || count == 0 || sample_rate <= 0) return -70.0;

  // Discrete Fourier magnitude accumulation across band
  const std::size_t n_bins = std::min(count / 2, static_cast<std::size_t>(256));
  double energy = 0.0;
  std::size_t bins_in_band = 0;

  const double bin_width = static_cast<double>(sample_rate) / static_cast<double>(count);
  for (std::size_t k = 1; k < n_bins; ++k) {
    const double freq = static_cast<double>(k) * bin_width;
    if (freq >= f_low && freq < f_high) {
      double real = 0.0;
      double imag = 0.0;
      const std::size_t n_eval = std::min(count, static_cast<std::size_t>(512));
      for (std::size_t n = 0; n < n_eval; ++n) {
        const double angle = (2.0 * 3.1415926535 * static_cast<double>(k * n)) / static_cast<double>(count);
        real += static_cast<double>(mono[n]) * std::cos(angle);
        imag -= static_cast<double>(mono[n]) * std::sin(angle);
      }
      energy += (real * real + imag * imag) / static_cast<double>(n_eval * n_eval);
      ++bins_in_band;
    }
  }

  if (bins_in_band == 0 || energy <= 1e-12) return -70.0;
  return 10.0 * std::log10(energy / static_cast<double>(bins_in_band));
}

}  // namespace

ReferenceProfile ReferenceAnalyzer::extract_profile(
    const amt::audio::AudioBuffer& audio,
    const std::string& name,
    int sample_rate) const {
  ReferenceProfile profile{};
  profile.profile_id = "ref_" + std::to_string(audio.frames());
  profile.display_name = name;
  profile.source_track_name = name;
  profile.genre_tag = "reference";

  if (audio.frames() == 0) return profile;

  const std::size_t frames = audio.frames();
  const std::size_t channels = audio.channels();
  const int sr = sample_rate > 0 ? sample_rate : 44100;

  std::vector<float> mono(frames, 0.0f);
  for (std::size_t c = 0; c < channels; ++c) {
    const float* data = audio.channel(c).data();
    for (std::size_t i = 0; i < frames; ++i) {
      mono[i] += data[i] / static_cast<float>(channels);
    }
  }

  // Energy & Crest factor
  double peak_sq = 0.0;
  double sum_sq = 0.0;
  for (std::size_t i = 0; i < frames; ++i) {
    const double s = static_cast<double>(mono[i]);
    if (s * s > peak_sq) peak_sq = s * s;
    sum_sq += s * s;
  }
  const double rms = std::sqrt(sum_sq / static_cast<double>(frames));
  const double peak = std::sqrt(peak_sq);

  profile.integrated_lufs = rms > 1e-9 ? (20.0 * std::log10(rms) - 0.691) : -70.0;
  profile.true_peak_dbtp = peak > 1e-9 ? (20.0 * std::log10(peak)) : -70.0;
  profile.crest_factor_db = (rms > 1e-9 && peak > 1e-9) ? (20.0 * std::log10(peak / rms)) : 10.0;
  profile.loudness_range_lu = 6.5;
  profile.stereo_width = 1.0;

  // Multi-band spectral profile
  profile.spectrum.sub_db = compute_band_energy(mono.data(), frames, sr, 20.0, 60.0);
  profile.spectrum.bass_db = compute_band_energy(mono.data(), frames, sr, 60.0, 250.0);
  profile.spectrum.low_mid_db = compute_band_energy(mono.data(), frames, sr, 250.0, 500.0);
  profile.spectrum.mid_db = compute_band_energy(mono.data(), frames, sr, 500.0, 2000.0);
  profile.spectrum.high_mid_db = compute_band_energy(mono.data(), frames, sr, 2000.0, 6000.0);
  profile.spectrum.presence_db = compute_band_energy(mono.data(), frames, sr, 6000.0, 12000.0);
  profile.spectrum.air_db = compute_band_energy(mono.data(), frames, sr, 12000.0, 20000.0);

  profile.low_frequency_curve = {profile.spectrum.sub_db, profile.spectrum.bass_db, profile.spectrum.low_mid_db};

  return profile;
}

}  // namespace amt::reference
