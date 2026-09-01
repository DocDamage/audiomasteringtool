#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::dsp {

struct GainParams {
  double gain_db{0.0};
};

enum class EqShape { low_shelf, peak, high_shelf, high_pass, low_pass };

struct EqBand {
  EqShape shape{EqShape::peak};
  double frequency_hz{1000.0};
  double gain_db{0.0};
  double q{0.707};
};

struct EqParams {
  std::vector<EqBand> bands;
};

struct CompressorParams {
  double threshold_db{-18.0};
  double ratio{1.5};
  double attack_ms{20.0};
  double release_ms{120.0};
  double knee_db{6.0};
  double makeup_db{0.0};
  double mix{1.0};
};

struct DynamicEqParams {
  double frequency_hz{80.0};
  double q{1.0};
  double threshold_db{-22.0};
  double ratio{2.0};
  double attack_ms{15.0};
  double release_ms{140.0};
  double max_reduction_db{3.0};
};

struct MultibandParams {
  double low_crossover_hz{120.0};
  double high_crossover_hz{5000.0};
  double low_threshold_db{-18.0};
  double mid_threshold_db{-16.0};
  double high_threshold_db{-18.0};
  double low_ratio{1.4};
  double mid_ratio{1.3};
  double high_ratio{1.4};
  double attack_ms{25.0};
  double release_ms{160.0};
};

struct TransientParams {
  double attack_db{0.0};
  double sustain_db{0.0};
  double fast_ms{3.0};
  double slow_ms{35.0};
  double mix{1.0};
};

struct SaturationParams {
  double drive_db{0.0};
  double mix{0.0};
};

struct StereoParams {
  double width{1.0};
  double bass_mono_hz{90.0};
};

struct ClipperParams {
  double threshold_db{-1.0};
  double softness{0.25};
};

struct LimiterParams {
  double ceiling_db{-1.0};
  double release_ms{80.0};
};

using ProcessorParams = std::variant<GainParams, EqParams, CompressorParams, DynamicEqParams,
                                     MultibandParams, TransientParams, SaturationParams,
                                     StereoParams, ClipperParams, LimiterParams>;

struct ProcessorSpec {
  std::string id;
  bool bypass{false};
  ProcessorParams params;
};

class IProcessor {
 public:
  virtual ~IProcessor() = default;
  virtual void reset(int sample_rate, std::size_t channels) = 0;
  virtual void process(amt::audio::AudioBuffer& buffer) = 0;
};

[[nodiscard]] std::unique_ptr<IProcessor> make_processor(const ProcessorSpec& spec);
[[nodiscard]] std::string processor_type_name(const ProcessorParams& params);
[[nodiscard]] bool validate_processor_spec(const ProcessorSpec& spec, std::string& error);

}  // namespace amt::dsp
