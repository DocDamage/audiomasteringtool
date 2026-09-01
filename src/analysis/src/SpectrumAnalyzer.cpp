#include "amt/analysis/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace amt::analysis {
namespace {

bool is_power_of_two(const std::size_t value) {
  return value >= 2U && (value & (value - 1U)) == 0U;
}

void fft(std::vector<std::complex<double>>& values) {
  const std::size_t size = values.size();
  for (std::size_t i = 1U, j = 0U; i < size; ++i) {
    std::size_t bit = size >> 1U;
    for (; (j & bit) != 0U; bit >>= 1U) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(values[i], values[j]);
  }
  for (std::size_t length = 2U; length <= size; length <<= 1U) {
    const double angle = -2.0 * std::numbers::pi / static_cast<double>(length);
    const std::complex<double> step(std::cos(angle), std::sin(angle));
    for (std::size_t offset = 0U; offset < size; offset += length) {
      std::complex<double> rotation(1.0, 0.0);
      for (std::size_t index = 0U; index < length / 2U; ++index) {
        const auto even = values[offset + index];
        const auto odd = values[offset + index + length / 2U] * rotation;
        values[offset + index] = even + odd;
        values[offset + index + length / 2U] = even - odd;
        rotation *= step;
      }
    }
  }
}

}  // namespace

struct SpectrumAnalyzer::Impl {
  int sample_rate{0};
  std::size_t channels{0};
  std::size_t fft_size{0};
  std::size_t hop_size{0};
  std::vector<double> pending;
  std::vector<double> accumulated_power;
  std::uint64_t windows{0};

  Impl(const int rate, const std::size_t channel_count, const std::size_t size)
      : sample_rate(rate), channels(channel_count), fft_size(size), hop_size(size / 2U),
        accumulated_power(size / 2U + 1U, 0.0) {
    if (sample_rate <= 0 || channels == 0U || !is_power_of_two(fft_size)) {
      throw std::invalid_argument("invalid spectrum analyzer configuration");
    }
  }

  void analyze_window(const std::vector<double>& source, const bool zero_pad) {
    std::vector<std::complex<double>> values(fft_size);
    const auto available = std::min(source.size(), fft_size);
    for (std::size_t index = 0; index < available; ++index) {
      const double window = 0.5 - 0.5 *
          std::cos(2.0 * std::numbers::pi * static_cast<double>(index) /
                   static_cast<double>(fft_size - 1U));
      values[index] = source[index] * window;
    }
    if (!zero_pad && available < fft_size) return;
    fft(values);
    for (std::size_t bin = 0; bin < accumulated_power.size(); ++bin) {
      accumulated_power[bin] += std::norm(values[bin]);
    }
    ++windows;
  }
};

SpectrumAnalyzer::SpectrumAnalyzer(
    const int sample_rate, const std::size_t channels, const std::size_t fft_size)
    : impl_(std::make_unique<Impl>(sample_rate, channels, fft_size)) {}
SpectrumAnalyzer::~SpectrumAnalyzer() = default;
SpectrumAnalyzer::SpectrumAnalyzer(SpectrumAnalyzer&&) noexcept = default;
SpectrumAnalyzer& SpectrumAnalyzer::operator=(SpectrumAnalyzer&&) noexcept = default;

void SpectrumAnalyzer::process(const amt::audio::AudioBuffer& buffer) {
  if (buffer.channels() != impl_->channels) throw std::invalid_argument("spectrum channel mismatch");
  for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
    double mono = 0.0;
    for (std::size_t channel = 0; channel < impl_->channels; ++channel) {
      mono += static_cast<double>(buffer.channel(channel)[frame]);
    }
    impl_->pending.push_back(mono / static_cast<double>(impl_->channels));
  }
  while (impl_->pending.size() >= impl_->fft_size) {
    impl_->analyze_window(impl_->pending, false);
    impl_->pending.erase(impl_->pending.begin(),
                         impl_->pending.begin() + static_cast<std::ptrdiff_t>(impl_->hop_size));
  }
}

SpectrumMetrics SpectrumAnalyzer::finalize() {
  if (impl_->windows == 0U && !impl_->pending.empty()) impl_->analyze_window(impl_->pending, true);
  SpectrumMetrics result;
  if (impl_->windows == 0U) return result;

  double total = 0.0;
  double weighted_frequency = 0.0;
  for (std::size_t bin = 0; bin < impl_->accumulated_power.size(); ++bin) {
    const double frequency = static_cast<double>(bin) * static_cast<double>(impl_->sample_rate) /
                             static_cast<double>(impl_->fft_size);
    const double power = impl_->accumulated_power[bin];
    total += power;
    weighted_frequency += frequency * power;
  }
  result.centroid_hz = total > 0.0 ? weighted_frequency / total : 0.0;

  const double rolloff_target = total * 0.85;
  double cumulative = 0.0;
  for (std::size_t bin = 0; bin < impl_->accumulated_power.size(); ++bin) {
    cumulative += impl_->accumulated_power[bin];
    if (cumulative >= rolloff_target) {
      result.rolloff_85_hz = static_cast<double>(bin) * static_cast<double>(impl_->sample_rate) /
                             static_cast<double>(impl_->fft_size);
      break;
    }
  }

  const std::vector<std::pair<double, double>> boundaries = {
      {20.0, 60.0}, {60.0, 250.0}, {250.0, 500.0}, {500.0, 2000.0},
      {2000.0, 6000.0}, {6000.0, std::min(20000.0, impl_->sample_rate * 0.5)}};
  for (const auto& [low, high] : boundaries) {
    double band_power = 0.0;
    for (std::size_t bin = 0; bin < impl_->accumulated_power.size(); ++bin) {
      const double frequency = static_cast<double>(bin) * static_cast<double>(impl_->sample_rate) /
                               static_cast<double>(impl_->fft_size);
      if (frequency >= low && frequency < high) band_power += impl_->accumulated_power[bin];
    }
    result.bands.push_back(
        {.low_hz = low, .high_hz = high, .energy_ratio = total > 0.0 ? band_power / total : 0.0});
  }
  return result;
}

}  // namespace amt::analysis
