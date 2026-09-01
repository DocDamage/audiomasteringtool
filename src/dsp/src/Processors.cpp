#include "amt/dsp/Processors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace amt::dsp {
namespace {

constexpr double kMinLinear = 1.0e-12;

double db_to_linear(const double db) { return std::pow(10.0, db / 20.0); }
double linear_to_db(const double value) {
  return 20.0 * std::log10(std::max(value, kMinLinear));
}
double coefficient(const double milliseconds, const int sample_rate) {
  if (milliseconds <= 0.0) return 0.0;
  return std::exp(-1.0 / (0.001 * milliseconds * static_cast<double>(sample_rate)));
}
double clamp_frequency(const double frequency, const int sample_rate) {
  return std::clamp(frequency, 10.0, static_cast<double>(sample_rate) * 0.49);
}

struct BiquadState {
  double z1{0.0};
  double z2{0.0};
};

class Biquad {
 public:
  void reset(const std::size_t channels) { states_.assign(channels, {}); }

  void configure(const EqBand& band, const int sample_rate) {
    const double frequency = clamp_frequency(band.frequency_hz, sample_rate);
    const double q = std::clamp(band.q, 0.1, 12.0);
    const double w0 = 2.0 * std::numbers::pi * frequency / static_cast<double>(sample_rate);
    const double c = std::cos(w0);
    const double s = std::sin(w0);
    const double alpha = s / (2.0 * q);
    const double a = std::pow(10.0, band.gain_db / 40.0);

    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;

    switch (band.shape) {
      case EqShape::peak:
        b0 = 1.0 + alpha * a;
        b1 = -2.0 * c;
        b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a;
        a1 = -2.0 * c;
        a2 = 1.0 - alpha / a;
        break;
      case EqShape::low_pass:
        b0 = (1.0 - c) * 0.5;
        b1 = 1.0 - c;
        b2 = (1.0 - c) * 0.5;
        a0 = 1.0 + alpha;
        a1 = -2.0 * c;
        a2 = 1.0 - alpha;
        break;
      case EqShape::high_pass:
        b0 = (1.0 + c) * 0.5;
        b1 = -(1.0 + c);
        b2 = (1.0 + c) * 0.5;
        a0 = 1.0 + alpha;
        a1 = -2.0 * c;
        a2 = 1.0 - alpha;
        break;
      case EqShape::low_shelf: {
        const double root_a = std::sqrt(a);
        const double shelf_alpha = s * std::sqrt(2.0) * 0.5;
        b0 = a * ((a + 1.0) - (a - 1.0) * c + 2.0 * root_a * shelf_alpha);
        b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * c);
        b2 = a * ((a + 1.0) - (a - 1.0) * c - 2.0 * root_a * shelf_alpha);
        a0 = (a + 1.0) + (a - 1.0) * c + 2.0 * root_a * shelf_alpha;
        a1 = -2.0 * ((a - 1.0) + (a + 1.0) * c);
        a2 = (a + 1.0) + (a - 1.0) * c - 2.0 * root_a * shelf_alpha;
        break;
      }
      case EqShape::high_shelf: {
        const double root_a = std::sqrt(a);
        const double shelf_alpha = s * std::sqrt(2.0) * 0.5;
        b0 = a * ((a + 1.0) + (a - 1.0) * c + 2.0 * root_a * shelf_alpha);
        b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * c);
        b2 = a * ((a + 1.0) + (a - 1.0) * c - 2.0 * root_a * shelf_alpha);
        a0 = (a + 1.0) - (a - 1.0) * c + 2.0 * root_a * shelf_alpha;
        a1 = 2.0 * ((a - 1.0) - (a + 1.0) * c);
        a2 = (a + 1.0) - (a - 1.0) * c - 2.0 * root_a * shelf_alpha;
        break;
      }
    }
    set_coefficients(b0, b1, b2, a0, a1, a2);
  }

  void configure_bandpass(const double frequency_hz, const double q, const int sample_rate) {
    const double frequency = clamp_frequency(frequency_hz, sample_rate);
    const double w0 = 2.0 * std::numbers::pi * frequency / static_cast<double>(sample_rate);
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::clamp(q, 0.1, 12.0));
    set_coefficients(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
  }

  double process(const std::size_t channel, const double input) {
    auto& state = states_.at(channel);
    const double output = b0_ * input + state.z1;
    state.z1 = b1_ * input - a1_ * output + state.z2;
    state.z2 = b2_ * input - a2_ * output;
    return output;
  }

 private:
  void set_coefficients(const double b0, const double b1, const double b2,
                        const double a0, const double a1, const double a2) {
    b0_ = b0 / a0;
    b1_ = b1 / a0;
    b2_ = b2 / a0;
    a1_ = a1 / a0;
    a2_ = a2 / a0;
  }

  double b0_{1.0};
  double b1_{0.0};
  double b2_{0.0};
  double a1_{0.0};
  double a2_{0.0};
  std::vector<BiquadState> states_;
};

class GainProcessor final : public IProcessor {
 public:
  explicit GainProcessor(GainParams params) : params_(params) {}
  void reset(int, std::size_t) override {}
  void process(amt::audio::AudioBuffer& buffer) override {
    const float gain = static_cast<float>(db_to_linear(params_.gain_db));
    for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
      for (float& sample : buffer.channel(channel)) sample *= gain;
    }
  }
 private:
  GainParams params_;
};

class EqProcessor final : public IProcessor {
 public:
  explicit EqProcessor(EqParams params) : params_(std::move(params)) {}
  void reset(const int sample_rate, const std::size_t channels) override {
    filters_.assign(params_.bands.size(), {});
    for (std::size_t index = 0; index < filters_.size(); ++index) {
      filters_[index].configure(params_.bands[index], sample_rate);
      filters_[index].reset(channels);
    }
  }
  void process(amt::audio::AudioBuffer& buffer) override {
    for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
      auto samples = buffer.channel(channel);
      for (float& sample : samples) {
        double value = sample;
        for (auto& filter : filters_) value = filter.process(channel, value);
        sample = static_cast<float>(value);
      }
    }
  }
 private:
  EqParams params_;
  std::vector<Biquad> filters_;
};

double compression_gain_db(const double level_db, const double threshold_db,
                           const double ratio, const double knee_db) {
  const double safe_ratio = std::max(ratio, 1.0);
  const double over = level_db - threshold_db;
  if (knee_db <= 0.0) return over > 0.0 ? (1.0 / safe_ratio - 1.0) * over : 0.0;
  const double half = knee_db * 0.5;
  if (over <= -half) return 0.0;
  if (over >= half) return (1.0 / safe_ratio - 1.0) * over;
  const double x = over + half;
  return (1.0 / safe_ratio - 1.0) * x * x / (2.0 * knee_db);
}

class CompressorProcessor final : public IProcessor {
 public:
  explicit CompressorProcessor(CompressorParams params) : params_(params) {}
  void reset(const int sample_rate, std::size_t) override {
    attack_ = coefficient(params_.attack_ms, sample_rate);
    release_ = coefficient(params_.release_ms, sample_rate);
    gain_db_ = 0.0;
  }
  void process(amt::audio::AudioBuffer& buffer) override {
    const double wet_mix = std::clamp(params_.mix, 0.0, 1.0);
    const double makeup = db_to_linear(params_.makeup_db);
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      double peak = 0.0;
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        peak = std::max(peak, std::abs(static_cast<double>(buffer.channel(channel)[frame])));
      }
      const double target = compression_gain_db(linear_to_db(peak), params_.threshold_db,
                                                params_.ratio, params_.knee_db);
      const double smoothing = target < gain_db_ ? attack_ : release_;
      gain_db_ = smoothing * gain_db_ + (1.0 - smoothing) * target;
      const double wet_gain = db_to_linear(gain_db_) * makeup;
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        const double dry = buffer.channel(channel)[frame];
        const double wet = dry * wet_gain;
        buffer.channel(channel)[frame] = static_cast<float>(dry + (wet - dry) * wet_mix);
      }
    }
  }
 private:
  CompressorParams params_;
  double attack_{0.0};
  double release_{0.0};
  double gain_db_{0.0};
};

class DynamicEqProcessor final : public IProcessor {
 public:
  explicit DynamicEqProcessor(DynamicEqParams params) : params_(params) {}
  void reset(const int sample_rate, const std::size_t channels) override {
    detector_.configure_bandpass(params_.frequency_hz, params_.q, sample_rate);
    detector_.reset(channels);
    attack_ = coefficient(params_.attack_ms, sample_rate);
    release_ = coefficient(params_.release_ms, sample_rate);
    gain_db_ = 0.0;
    band_samples_.assign(channels, 0.0);
  }
  void process(amt::audio::AudioBuffer& buffer) override {
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      double peak = 0.0;
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        const double band = detector_.process(channel, buffer.channel(channel)[frame]);
        band_samples_[channel] = band;
        peak = std::max(peak, std::abs(band));
      }
      double target = compression_gain_db(linear_to_db(peak), params_.threshold_db,
                                          params_.ratio, 3.0);
      target = std::max(target, -std::abs(params_.max_reduction_db));
      const double smoothing = target < gain_db_ ? attack_ : release_;
      gain_db_ = smoothing * gain_db_ + (1.0 - smoothing) * target;
      const double band_gain = db_to_linear(gain_db_);
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        const double input = buffer.channel(channel)[frame];
        const double output = input + band_samples_[channel] * (band_gain - 1.0);
        buffer.channel(channel)[frame] = static_cast<float>(output);
      }
    }
  }
 private:
  DynamicEqParams params_;
  Biquad detector_;
  std::vector<double> band_samples_;
  double attack_{0.0};
  double release_{0.0};
  double gain_db_{0.0};
};

struct BandGain {
  double threshold{-18.0};
  double ratio{1.5};
  double gain_db{0.0};
};

class MultibandProcessor final : public IProcessor {
 public:
  explicit MultibandProcessor(MultibandParams params) : params_(params) {}
  void reset(const int sample_rate, const std::size_t channels) override {
    const EqBand low{.shape = EqShape::low_pass, .frequency_hz = params_.low_crossover_hz,
                     .gain_db = 0.0, .q = 0.70710678};
    const EqBand high{.shape = EqShape::high_pass, .frequency_hz = params_.high_crossover_hz,
                      .gain_db = 0.0, .q = 0.70710678};
    for (auto& filter : low_) { filter.configure(low, sample_rate); filter.reset(channels); }
    for (auto& filter : high_) { filter.configure(high, sample_rate); filter.reset(channels); }
    bands_[0] = {.threshold = params_.low_threshold_db, .ratio = params_.low_ratio};
    bands_[1] = {.threshold = params_.mid_threshold_db, .ratio = params_.mid_ratio};
    bands_[2] = {.threshold = params_.high_threshold_db, .ratio = params_.high_ratio};
    attack_ = coefficient(params_.attack_ms, sample_rate);
    release_ = coefficient(params_.release_ms, sample_rate);
    scratch_.assign(channels, {0.0, 0.0, 0.0});
  }
  void process(amt::audio::AudioBuffer& buffer) override {
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      std::array<double, 3> peaks{};
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        const double input = buffer.channel(channel)[frame];
        const double low = low_[1].process(channel, low_[0].process(channel, input));
        const double high = high_[1].process(channel, high_[0].process(channel, input));
        const double mid = input - low - high;
        scratch_[channel] = {low, mid, high};
        peaks[0] = std::max(peaks[0], std::abs(low));
        peaks[1] = std::max(peaks[1], std::abs(mid));
        peaks[2] = std::max(peaks[2], std::abs(high));
      }
      std::array<double, 3> gains{};
      for (std::size_t band = 0; band < 3U; ++band) {
        const double target = compression_gain_db(linear_to_db(peaks[band]), bands_[band].threshold,
                                                  bands_[band].ratio, 4.0);
        const double smoothing = target < bands_[band].gain_db ? attack_ : release_;
        bands_[band].gain_db = smoothing * bands_[band].gain_db + (1.0 - smoothing) * target;
        gains[band] = db_to_linear(bands_[band].gain_db);
      }
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        const auto& band = scratch_[channel];
        buffer.channel(channel)[frame] = static_cast<float>(
            band[0] * gains[0] + band[1] * gains[1] + band[2] * gains[2]);
      }
    }
  }
 private:
  MultibandParams params_;
  std::array<Biquad, 2> low_;
  std::array<Biquad, 2> high_;
  std::array<BandGain, 3> bands_;
  std::vector<std::array<double, 3>> scratch_;
  double attack_{0.0};
  double release_{0.0};
};

class TransientProcessor final : public IProcessor {
 public:
  explicit TransientProcessor(TransientParams params) : params_(params) {}
  void reset(const int sample_rate, std::size_t) override {
    fast_coeff_ = coefficient(params_.fast_ms, sample_rate);
    slow_coeff_ = coefficient(params_.slow_ms, sample_rate);
    fast_env_ = 0.0;
    slow_env_ = 0.0;
  }
  void process(amt::audio::AudioBuffer& buffer) override {
    const double mix = std::clamp(params_.mix, 0.0, 1.0);
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      double peak = 0.0;
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        peak = std::max(peak, std::abs(static_cast<double>(buffer.channel(channel)[frame])));
      }
      fast_env_ = fast_coeff_ * fast_env_ + (1.0 - fast_coeff_) * peak;
      slow_env_ = slow_coeff_ * slow_env_ + (1.0 - slow_coeff_) * peak;
      const double shape = std::clamp((fast_env_ - slow_env_) / std::max(slow_env_, 1.0e-6), -1.0, 1.0);
      const double gain_db = shape >= 0.0 ? params_.attack_db * shape
                                          : params_.sustain_db * (-shape);
      const double wet_gain = db_to_linear(gain_db);
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        const double dry = buffer.channel(channel)[frame];
        buffer.channel(channel)[frame] = static_cast<float>(dry * (1.0 + (wet_gain - 1.0) * mix));
      }
    }
  }
 private:
  TransientParams params_;
  double fast_coeff_{0.0};
  double slow_coeff_{0.0};
  double fast_env_{0.0};
  double slow_env_{0.0};
};

class SaturationProcessor final : public IProcessor {
 public:
  explicit SaturationProcessor(SaturationParams params) : params_(params) {}
  void reset(int, std::size_t) override {}
  void process(amt::audio::AudioBuffer& buffer) override {
    const double drive = std::max(db_to_linear(params_.drive_db), 1.0);
    const double normalization = std::tanh(drive);
    const double mix = std::clamp(params_.mix, 0.0, 1.0);
    for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
      for (float& sample : buffer.channel(channel)) {
        const double dry = sample;
        const double wet = std::tanh(dry * drive) / normalization;
        sample = static_cast<float>(dry + (wet - dry) * mix);
      }
    }
  }
 private:
  SaturationParams params_;
};

class StereoProcessor final : public IProcessor {
 public:
  explicit StereoProcessor(StereoParams params) : params_(params) {}
  void reset(const int sample_rate, const std::size_t channels) override {
    sample_rate_ = sample_rate;
    channels_ = channels;
    low_side_ = 0.0;
    const double cutoff = clamp_frequency(params_.bass_mono_hz, sample_rate_);
    low_coeff_ = std::exp(-2.0 * std::numbers::pi * cutoff / static_cast<double>(sample_rate_));
  }
  void process(amt::audio::AudioBuffer& buffer) override {
    if (channels_ != 2U || buffer.channels() != 2U) return;
    const double width = std::clamp(params_.width, 0.0, 2.0);
    auto left = buffer.channel(0U);
    auto right = buffer.channel(1U);
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      const double mid = (static_cast<double>(left[frame]) + right[frame]) * 0.5;
      const double side = (static_cast<double>(left[frame]) - right[frame]) * 0.5;
      low_side_ = low_coeff_ * low_side_ + (1.0 - low_coeff_) * side;
      const double high_side = side - low_side_;
      const double output_side = high_side * width + low_side_ * std::min(width, 0.08);
      left[frame] = static_cast<float>(mid + output_side);
      right[frame] = static_cast<float>(mid - output_side);
    }
  }
 private:
  StereoParams params_;
  int sample_rate_{0};
  std::size_t channels_{0};
  double low_coeff_{0.0};
  double low_side_{0.0};
};

class ClipperProcessor final : public IProcessor {
 public:
  explicit ClipperProcessor(ClipperParams params) : params_(params) {}
  void reset(int, std::size_t) override {}
  void process(amt::audio::AudioBuffer& buffer) override {
    const double threshold = db_to_linear(params_.threshold_db);
    const double softness = std::clamp(params_.softness, 0.0, 0.95);
    const double knee = threshold * (1.0 - softness);
    for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
      for (float& sample : buffer.channel(channel)) {
        const double input = sample;
        const double magnitude = std::abs(input);
        double output = input;
        if (magnitude >= threshold) {
          output = std::copysign(threshold, input);
        } else if (softness > 0.0 && magnitude > knee) {
          const double t = (magnitude - knee) / std::max(threshold - knee, 1.0e-9);
          const double shaped = knee + (threshold - knee) * (1.0 - (1.0 - t) * (1.0 - t));
          output = std::copysign(shaped, input);
        }
        sample = static_cast<float>(output);
      }
    }
  }
 private:
  ClipperParams params_;
};

class LimiterProcessor final : public IProcessor {
 public:
  explicit LimiterProcessor(LimiterParams params) : params_(params) {}
  void reset(const int sample_rate, std::size_t) override {
    release_ = coefficient(params_.release_ms, sample_rate);
    gain_ = 1.0;
  }
  void process(amt::audio::AudioBuffer& buffer) override {
    const double ceiling = db_to_linear(params_.ceiling_db);
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      double peak = 0.0;
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        peak = std::max(peak, std::abs(static_cast<double>(buffer.channel(channel)[frame])));
      }
      const double target = peak > ceiling ? ceiling / peak : 1.0;
      if (target < gain_) gain_ = target;
      else gain_ = release_ * gain_ + (1.0 - release_) * target;
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        const double value = static_cast<double>(buffer.channel(channel)[frame]) * gain_;
        buffer.channel(channel)[frame] = static_cast<float>(std::clamp(value, -ceiling, ceiling));
      }
    }
  }
 private:
  LimiterParams params_;
  double release_{0.0};
  double gain_{1.0};
};

template <typename T>
bool finite_params(const T&) { return true; }

}  // namespace

std::unique_ptr<IProcessor> make_processor(const ProcessorSpec& spec) {
  return std::visit([](const auto& params) -> std::unique_ptr<IProcessor> {
    using T = std::decay_t<decltype(params)>;
    if constexpr (std::is_same_v<T, GainParams>) return std::make_unique<GainProcessor>(params);
    else if constexpr (std::is_same_v<T, EqParams>) return std::make_unique<EqProcessor>(params);
    else if constexpr (std::is_same_v<T, CompressorParams>) return std::make_unique<CompressorProcessor>(params);
    else if constexpr (std::is_same_v<T, DynamicEqParams>) return std::make_unique<DynamicEqProcessor>(params);
    else if constexpr (std::is_same_v<T, MultibandParams>) return std::make_unique<MultibandProcessor>(params);
    else if constexpr (std::is_same_v<T, TransientParams>) return std::make_unique<TransientProcessor>(params);
    else if constexpr (std::is_same_v<T, SaturationParams>) return std::make_unique<SaturationProcessor>(params);
    else if constexpr (std::is_same_v<T, StereoParams>) return std::make_unique<StereoProcessor>(params);
    else if constexpr (std::is_same_v<T, ClipperParams>) return std::make_unique<ClipperProcessor>(params);
    else return std::make_unique<LimiterProcessor>(params);
  }, spec.params);
}

std::string processor_type_name(const ProcessorParams& params) {
  return std::visit([](const auto& value) -> std::string {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, GainParams>) return "gain";
    else if constexpr (std::is_same_v<T, EqParams>) return "eq";
    else if constexpr (std::is_same_v<T, CompressorParams>) return "compressor";
    else if constexpr (std::is_same_v<T, DynamicEqParams>) return "dynamic_eq";
    else if constexpr (std::is_same_v<T, MultibandParams>) return "multiband";
    else if constexpr (std::is_same_v<T, TransientParams>) return "transient";
    else if constexpr (std::is_same_v<T, SaturationParams>) return "saturation";
    else if constexpr (std::is_same_v<T, StereoParams>) return "stereo";
    else if constexpr (std::is_same_v<T, ClipperParams>) return "clipper";
    else return "limiter";
  }, params);
}

bool validate_processor_spec(const ProcessorSpec& spec, std::string& error) {
  if (spec.id.empty()) {
    error = "processor id cannot be empty";
    return false;
  }
  return std::visit([&](const auto& params) {
    using T = std::decay_t<decltype(params)>;
    if (!finite_params(params)) {
      error = "processor parameters are not finite";
      return false;
    }
    if constexpr (std::is_same_v<T, CompressorParams>) {
      if (params.ratio < 1.0 || params.mix < 0.0 || params.mix > 1.0) {
        error = "invalid compressor parameters";
        return false;
      }
    } else if constexpr (std::is_same_v<T, DynamicEqParams>) {
      if (params.ratio < 1.0 || params.q <= 0.0 || params.max_reduction_db < 0.0) {
        error = "invalid dynamic EQ parameters";
        return false;
      }
    } else if constexpr (std::is_same_v<T, MultibandParams>) {
      if (params.low_crossover_hz <= 0.0 || params.high_crossover_hz <= params.low_crossover_hz ||
          params.low_ratio < 1.0 || params.mid_ratio < 1.0 || params.high_ratio < 1.0) {
        error = "invalid multiband parameters";
        return false;
      }
    } else if constexpr (std::is_same_v<T, TransientParams>) {
      if (params.fast_ms <= 0.0 || params.slow_ms <= params.fast_ms ||
          params.mix < 0.0 || params.mix > 1.0) {
        error = "invalid transient parameters";
        return false;
      }
    } else if constexpr (std::is_same_v<T, SaturationParams>) {
      if (params.mix < 0.0 || params.mix > 1.0) {
        error = "invalid saturation mix";
        return false;
      }
    } else if constexpr (std::is_same_v<T, StereoParams>) {
      if (params.width < 0.0 || params.width > 2.0 || params.bass_mono_hz < 0.0) {
        error = "invalid stereo parameters";
        return false;
      }
    } else if constexpr (std::is_same_v<T, ClipperParams>) {
      if (params.softness < 0.0 || params.softness >= 1.0) {
        error = "invalid clipper softness";
        return false;
      }
    } else if constexpr (std::is_same_v<T, LimiterParams>) {
      if (params.release_ms <= 0.0) {
        error = "invalid limiter release";
        return false;
      }
    } else if constexpr (std::is_same_v<T, EqParams>) {
      for (const auto& band : params.bands) {
        if (band.frequency_hz <= 0.0 || band.q <= 0.0) {
          error = "invalid EQ band";
          return false;
        }
      }
    }
    return true;
  }, spec.params);
}

}  // namespace amt::dsp
