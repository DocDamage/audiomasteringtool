#include "amt/analysis/StereoAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace amt::analysis {
namespace {

struct OnePoleLowPass {
  double coefficient{0.0};
  double state{0.0};
  double process(const double input) {
    state += coefficient * (input - state);
    return state;
  }
};

struct BandEnergy {
  double mid_square{0.0};
  double side_square{0.0};
};

double width(const BandEnergy& band) {
  const double total = band.mid_square + band.side_square;
  return total > 0.0 ? band.side_square / total : 0.0;
}

}  // namespace

struct StereoAnalyzer::Impl {
  int sample_rate{0};
  std::size_t channels{0};
  OnePoleLowPass left_200;
  OnePoleLowPass left_4000;
  OnePoleLowPass right_200;
  OnePoleLowPass right_4000;
  std::array<BandEnergy, 3> bands{};
  double sum_left_square{0.0};
  double sum_right_square{0.0};
  double sum_left_right{0.0};
  double sum_mono_square{0.0};
  std::uint64_t frames{0};
  double window_left_square{0.0};
  double window_right_square{0.0};
  double window_left_right{0.0};
  std::uint32_t window_frames{0};
  std::uint64_t correlation_windows{0};
  std::uint64_t negative_windows{0};

  Impl(const int rate, const std::size_t channel_count)
      : sample_rate(rate), channels(channel_count) {
    if (sample_rate <= 0 || channels == 0U) throw std::invalid_argument("invalid stereo analyzer");
    const auto coefficient = [this](const double cutoff) {
      return 1.0 - std::exp(-2.0 * 3.14159265358979323846 * cutoff /
                            static_cast<double>(sample_rate));
    };
    left_200.coefficient = right_200.coefficient = coefficient(200.0);
    left_4000.coefficient = right_4000.coefficient = coefficient(4000.0);
  }

  void consume_band(const std::size_t index, const double left, const double right) {
    constexpr double scale = 0.70710678118654752440;
    const double mid = (left + right) * scale;
    const double side = (left - right) * scale;
    bands[index].mid_square += mid * mid;
    bands[index].side_square += side * side;
  }

  void finish_window() {
    if (window_frames == 0U) return;
    const double denominator = std::sqrt(window_left_square * window_right_square);
    if (denominator > 0.0) {
      if (window_left_right / denominator < 0.0) ++negative_windows;
      ++correlation_windows;
    }
    window_left_square = window_right_square = window_left_right = 0.0;
    window_frames = 0U;
  }
};

StereoAnalyzer::StereoAnalyzer(const int sample_rate, const std::size_t channels)
    : impl_(std::make_unique<Impl>(sample_rate, channels)) {}
StereoAnalyzer::~StereoAnalyzer() = default;
StereoAnalyzer::StereoAnalyzer(StereoAnalyzer&&) noexcept = default;
StereoAnalyzer& StereoAnalyzer::operator=(StereoAnalyzer&&) noexcept = default;

void StereoAnalyzer::process(const amt::audio::AudioBuffer& buffer) {
  if (buffer.channels() != impl_->channels) throw std::invalid_argument("stereo channel mismatch");
  if (impl_->channels < 2U) {
    impl_->frames += buffer.frames();
    return;
  }
  const auto left_channel = buffer.channel(0U);
  const auto right_channel = buffer.channel(1U);
  for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
    const double left = left_channel[frame];
    const double right = right_channel[frame];
    const double left_low = impl_->left_200.process(left);
    const double right_low = impl_->right_200.process(right);
    const double left_low_mid = impl_->left_4000.process(left);
    const double right_low_mid = impl_->right_4000.process(right);
    impl_->consume_band(0U, left_low, right_low);
    impl_->consume_band(1U, left_low_mid - left_low, right_low_mid - right_low);
    impl_->consume_band(2U, left - left_low_mid, right - right_low_mid);

    impl_->sum_left_square += left * left;
    impl_->sum_right_square += right * right;
    impl_->sum_left_right += left * right;
    const double mono = (left + right) * 0.5;
    impl_->sum_mono_square += mono * mono;
    impl_->window_left_square += left * left;
    impl_->window_right_square += right * right;
    impl_->window_left_right += left * right;
    ++impl_->window_frames;
    ++impl_->frames;
    if (impl_->window_frames >= 2048U) impl_->finish_window();
  }
}

StereoMetrics StereoAnalyzer::finalize() const {
  StereoMetrics result;
  if (impl_->channels < 2U || impl_->frames == 0U) return result;
  impl_->finish_window();
  const double denominator = std::sqrt(impl_->sum_left_square * impl_->sum_right_square);
  result.correlation = denominator > 0.0 ? impl_->sum_left_right / denominator : 1.0;
  result.low_band_width = width(impl_->bands[0]);
  result.mid_band_width = width(impl_->bands[1]);
  result.high_band_width = width(impl_->bands[2]);
  const double stereo_reference = (impl_->sum_left_square + impl_->sum_right_square) * 0.5;
  if (stereo_reference > 0.0) {
    result.mono_fold_down_delta_db = impl_->sum_mono_square > 0.0
        ? 10.0 * std::log10(impl_->sum_mono_square / stereo_reference)
        : -200.0;
  }
  result.negative_correlation_window_fraction = impl_->correlation_windows > 0U
      ? static_cast<double>(impl_->negative_windows) /
            static_cast<double>(impl_->correlation_windows)
      : 0.0;
  return result;
}

}  // namespace amt::analysis
