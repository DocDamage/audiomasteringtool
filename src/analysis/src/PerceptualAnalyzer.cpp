#include "amt/analysis/PerceptualAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace amt::analysis {
namespace {

double power_db(const double value) {
  return 10.0 * std::log10(std::max(value, 1.0e-18));
}

double score_from_ratio(const double ratio, const double start, const double span) {
  return std::clamp((ratio - start) / span, 0.0, 1.0);
}

}  // namespace

struct PerceptualAnalyzer::Impl {
  int sample_rate{0};
  std::size_t channels{0U};
  std::size_t window_size{2048U};
  std::vector<double> pending;
  std::vector<double> frequencies;
  std::vector<double> sum_power;
  std::vector<std::size_t> prominence_windows;
  std::vector<double> first_seen;
  std::vector<double> last_seen;
  std::size_t windows{0U};
  std::int64_t processed_samples{0};

  Impl(const int rate, const std::size_t channel_count)
      : sample_rate(rate), channels(channel_count) {
    if (sample_rate <= 0 || channels == 0U) {
      throw std::invalid_argument("invalid perceptual analyzer configuration");
    }
    constexpr std::size_t count = 40U;
    const double low = 40.0;
    const double high = std::min(16000.0, static_cast<double>(sample_rate) * 0.46);
    const double ratio = std::pow(high / low, 1.0 / static_cast<double>(count - 1U));
    frequencies.resize(count);
    sum_power.assign(count, 0.0);
    prominence_windows.assign(count, 0U);
    first_seen.assign(count, -1.0);
    last_seen.assign(count, -1.0);
    double frequency = low;
    for (std::size_t index = 0U; index < count; ++index) {
      frequencies[index] = frequency;
      frequency *= ratio;
    }
  }

  double goertzel_power(const std::vector<double>& samples, const double frequency) const {
    const double omega = 2.0 * std::numbers::pi * frequency / static_cast<double>(sample_rate);
    const double coefficient = 2.0 * std::cos(omega);
    double q0 = 0.0;
    double q1 = 0.0;
    double q2 = 0.0;
    for (std::size_t index = 0U; index < samples.size(); ++index) {
      const double window = 0.5 - 0.5 * std::cos(
          2.0 * std::numbers::pi * static_cast<double>(index) /
          static_cast<double>(samples.size() - 1U));
      q0 = coefficient * q1 - q2 + samples[index] * window;
      q2 = q1;
      q1 = q0;
    }
    const double power = q1 * q1 + q2 * q2 - coefficient * q1 * q2;
    const double normalization = static_cast<double>(samples.size()) * static_cast<double>(samples.size());
    return std::max(power / std::max(normalization, 1.0), 0.0);
  }

  void analyze_window() {
    if (pending.size() < 32U) return;
    std::vector<double> powers(frequencies.size(), 0.0);
    std::vector<double> db(frequencies.size(), -180.0);
    for (std::size_t index = 0U; index < frequencies.size(); ++index) {
      powers[index] = goertzel_power(pending, frequencies[index]);
      db[index] = power_db(powers[index]);
      sum_power[index] += powers[index];
    }
    const double time = static_cast<double>(processed_samples) / static_cast<double>(sample_rate);
    for (std::size_t index = 1U; index + 1U < db.size(); ++index) {
      const double neighbor = (db[index - 1U] + db[index + 1U]) * 0.5;
      if (db[index] - neighbor >= 4.0) {
        ++prominence_windows[index];
        if (first_seen[index] < 0.0) first_seen[index] = time;
        last_seen[index] = time;
      }
    }
    ++windows;
    pending.clear();
  }
};

PerceptualAnalyzer::PerceptualAnalyzer(const int sample_rate, const std::size_t channels)
    : impl_(std::make_unique<Impl>(sample_rate, channels)) {}
PerceptualAnalyzer::~PerceptualAnalyzer() = default;
PerceptualAnalyzer::PerceptualAnalyzer(PerceptualAnalyzer&&) noexcept = default;
PerceptualAnalyzer& PerceptualAnalyzer::operator=(PerceptualAnalyzer&&) noexcept = default;

void PerceptualAnalyzer::process(const amt::audio::AudioBuffer& buffer) {
  if (buffer.channels() != impl_->channels) {
    throw std::invalid_argument("perceptual analyzer channel mismatch");
  }
  for (std::size_t frame = 0U; frame < buffer.frames(); ++frame) {
    double mono = 0.0;
    for (std::size_t channel = 0U; channel < impl_->channels; ++channel) {
      mono += static_cast<double>(buffer.channel(channel)[frame]);
    }
    impl_->pending.push_back(mono / static_cast<double>(impl_->channels));
    ++impl_->processed_samples;
    if (impl_->pending.size() >= impl_->window_size) impl_->analyze_window();
  }
}

PerceptualMetrics PerceptualAnalyzer::finalize() {
  if (!impl_->pending.empty()) impl_->analyze_window();
  PerceptualMetrics result;
  if (impl_->windows == 0U) return result;

  double total = 0.0;
  double sub = 0.0;
  double mud = 0.0;
  double harsh = 0.0;
  double bright = 0.0;
  std::vector<double> mean_db(impl_->frequencies.size(), -180.0);
  for (std::size_t index = 0U; index < impl_->frequencies.size(); ++index) {
    const double power = impl_->sum_power[index] / static_cast<double>(impl_->windows);
    mean_db[index] = power_db(power);
    const double frequency = impl_->frequencies[index];
    if (frequency >= 80.0 && frequency <= 14000.0) total += power;
    if (frequency >= 40.0 && frequency < 95.0) sub += power;
    if (frequency >= 150.0 && frequency < 450.0) mud += power;
    if (frequency >= 2500.0 && frequency < 6500.0) harsh += power;
    if (frequency >= 6500.0 && frequency <= 16000.0) bright += power;
  }
  const double denominator = std::max(total, 1.0e-18);
  const double sub_ratio = sub / denominator;
  const double mud_ratio = mud / denominator;
  const double harsh_ratio = harsh / denominator;
  const double bright_ratio = bright / denominator;
  result.sub_buildup_score = score_from_ratio(sub_ratio, 0.18, 0.35);
  result.mud_score = score_from_ratio(mud_ratio, 0.24, 0.32);
  result.harshness_score = score_from_ratio(harsh_ratio, 0.22, 0.30);
  result.brightness_score = score_from_ratio(bright_ratio, 0.16, 0.32);

  for (std::size_t index = 1U; index + 1U < mean_db.size(); ++index) {
    const double frequency = impl_->frequencies[index];
    if (frequency < 80.0 || frequency > 12000.0) continue;
    const double neighbor = (mean_db[index - 1U] + mean_db[index + 1U]) * 0.5;
    const double prominence = mean_db[index] - neighbor;
    const double persistence = static_cast<double>(impl_->prominence_windows[index]) /
                               static_cast<double>(impl_->windows);
    if (prominence < 2.5 || persistence < 0.08) continue;
    const double severity = std::clamp((prominence - 2.0) / 9.0 * (0.55 + persistence), 0.0, 1.0);
    result.resonances.push_back({.frequency_hz = frequency,
                                 .prominence_db = prominence,
                                 .persistence = persistence,
                                 .severity = severity,
                                 .first_seen_seconds = std::max(0.0, impl_->first_seen[index]),
                                 .last_seen_seconds = std::max(0.0, impl_->last_seen[index])});
  }
  std::sort(result.resonances.begin(), result.resonances.end(),
            [](const ResonanceCandidate& a, const ResonanceCandidate& b) {
              return a.severity > b.severity;
            });
  if (result.resonances.size() > 8U) result.resonances.resize(8U);
  for (const auto& candidate : result.resonances) {
    if (candidate.frequency_hz >= 2500.0 && candidate.frequency_hz <= 6500.0) {
      result.harshness_score = std::clamp(
          result.harshness_score + candidate.severity * candidate.persistence * 0.25, 0.0, 1.0);
    }
  }
  result.tonal_imbalance_score = std::max(
      {result.sub_buildup_score, result.mud_score, result.harshness_score,
       std::abs(result.brightness_score - 0.35) * 0.65});
  return result;
}

}  // namespace amt::analysis
