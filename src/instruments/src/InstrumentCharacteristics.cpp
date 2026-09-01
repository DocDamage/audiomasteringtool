#include "amt/instruments/InstrumentCharacteristics.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace amt::instruments {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

double estimate_f0(const float* samples, std::size_t count, int sample_rate) {
  if (samples == nullptr || count < 256 || sample_rate <= 0) return 0.0;

  const std::size_t min_lag = static_cast<std::size_t>(sample_rate / 1000);  // max 1000 Hz
  const std::size_t max_lag = static_cast<std::size_t>(sample_rate / 30);    // min 30 Hz
  if (max_lag >= count / 2) return 0.0;

  // Normalized autocorrelation
  double max_autocorr = -1.0;
  std::size_t best_lag = 0;

  double energy0 = 0.0;
  for (std::size_t i = 0; i < count / 2; ++i) {
    energy0 += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
  }
  if (energy0 <= 1e-12) return 0.0;

  for (std::size_t lag = min_lag; lag <= max_lag && lag < count / 2; ++lag) {
    double autocorr = 0.0;
    double energy_lag = 0.0;
    for (std::size_t i = 0; i < count / 2; ++i) {
      const double s0 = static_cast<double>(samples[i]);
      const double sl = static_cast<double>(samples[i + lag]);
      autocorr += s0 * sl;
      energy_lag += sl * sl;
    }
    const double norm = std::sqrt(energy0 * energy_lag);
    if (norm > 1e-12) {
      const double score = autocorr / norm;
      if (score > max_autocorr && score > 0.4) {
        max_autocorr = score;
        best_lag = lag;
      }
    }
  }

  if (best_lag > 0) {
    return static_cast<double>(sample_rate) / static_cast<double>(best_lag);
  }
  return 0.0;
}

double compute_spectral_centroid(const float* samples, std::size_t count, int sample_rate) {
  if (samples == nullptr || count < 64 || sample_rate <= 0) return 0.0;

  const std::size_t n_eval = std::min(count, static_cast<std::size_t>(1024));
  const std::size_t n_bins = n_eval / 2;
  double weighted_sum = 0.0;
  double total_magnitude = 0.0;

  for (std::size_t k = 1; k < n_bins; ++k) {
    const double freq = (static_cast<double>(k) * static_cast<double>(sample_rate)) / static_cast<double>(n_eval);
    double real = 0.0;
    double imag = 0.0;
    for (std::size_t n = 0; n < n_eval; ++n) {
      const double angle = (2.0 * kPi * static_cast<double>(k * n)) / static_cast<double>(n_eval);
      const double w = 0.5 * (1.0 - std::cos((2.0 * kPi * static_cast<double>(n)) / static_cast<double>(n_eval)));
      const double val = static_cast<double>(samples[n]) * w;
      real += val * std::cos(angle);
      imag -= val * std::sin(angle);
    }
    const double mag = std::sqrt(real * real + imag * imag);
    weighted_sum += freq * mag;
    total_magnitude += mag;
  }

  if (total_magnitude <= 1e-12) return 0.0;
  return weighted_sum / total_magnitude;
}

double compute_stereo_correlation(
    const amt::audio::AudioBuffer& buffer,
    std::size_t start_frame,
    std::size_t count) {
  if (buffer.channels() < 2 || buffer.frames() == 0) return 1.0;

  const std::size_t n = (count == 0 || start_frame + count > buffer.frames())
                            ? (buffer.frames() - start_frame)
                            : count;
  if (n == 0) return 1.0;

  const float* left = buffer.channel(0).data() + start_frame;
  const float* right = buffer.channel(1).data() + start_frame;

  double sum_l = 0.0;
  double sum_r = 0.0;
  double sum_ll = 0.0;
  double sum_rr = 0.0;
  double sum_lr = 0.0;

  for (std::size_t i = 0; i < n; ++i) {
    const double l = static_cast<double>(left[i]);
    const double r = static_cast<double>(right[i]);
    sum_l += l;
    sum_r += r;
    sum_ll += l * l;
    sum_rr += r * r;
    sum_lr += l * r;
  }

  const double num = static_cast<double>(n) * sum_lr - sum_l * sum_r;
  const double den_l = static_cast<double>(n) * sum_ll - sum_l * sum_l;
  const double den_r = static_cast<double>(n) * sum_rr - sum_r * sum_r;
  const double den = std::sqrt(std::max(0.0, den_l * den_r));

  if (den <= 1e-12) return 1.0;
  return std::clamp(num / den, -1.0, 1.0);
}

InstrumentCharacteristics extract_characteristics(
    const amt::audio::AudioBuffer& buffer,
    int sample_rate,
    std::size_t start_frame,
    std::size_t frame_count) {
  InstrumentCharacteristics charact{};
  if (buffer.frames() == 0) return charact;

  const std::size_t frames = (frame_count == 0 || start_frame + frame_count > buffer.frames())
                                 ? (buffer.frames() - start_frame)
                                 : frame_count;
  if (frames == 0) return charact;

  // Mixdown to mono for spectral / f0 analysis
  std::vector<float> mono(frames, 0.0f);
  const std::size_t channels = buffer.channels();
  for (std::size_t c = 0; c < channels; ++c) {
    const float* src = buffer.channel(c).data() + start_frame;
    for (std::size_t i = 0; i < frames; ++i) {
      mono[i] += src[i] / static_cast<float>(channels);
    }
  }

  charact.f0_hz = estimate_f0(mono.data(), frames, sample_rate);
  charact.spectral_centroid_hz = compute_spectral_centroid(mono.data(), frames, sample_rate);
  charact.stereo_correlation = compute_stereo_correlation(buffer, start_frame, frames);

  // Peak and RMS for crest factor
  double peak_sq = 0.0;
  double sum_sq = 0.0;
  for (std::size_t i = 0; i < frames; ++i) {
    const double val = static_cast<double>(mono[i]);
    const double sq = val * val;
    if (sq > peak_sq) peak_sq = sq;
    sum_sq += sq;
  }
  const double rms = std::sqrt(sum_sq / static_cast<double>(frames));
  const double peak = std::sqrt(peak_sq);
  if (rms > 1e-9 && peak > 1e-9) {
    charact.crest_factor_db = 20.0 * std::log10(peak / rms);
  }

  // Harmonicity estimation
  if (charact.f0_hz > 0.0) {
    charact.harmonicity_ratio = std::clamp(charact.crest_factor_db / 20.0, 0.2, 0.95);
  } else {
    charact.harmonicity_ratio = 0.1;
  }

  return charact;
}

}  // namespace amt::instruments
