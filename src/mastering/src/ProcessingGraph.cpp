#include "amt/mastering/ProcessingGraph.h"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace amt::mastering {
namespace {

std::string escape_json(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (const char value : input) {
    switch (value) {
      case '\\': output += "\\\\"; break;
      case '"': output += "\\\""; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default: output += value; break;
    }
  }
  return output;
}

const char* eq_shape_name(const amt::dsp::EqShape shape) {
  switch (shape) {
    case amt::dsp::EqShape::low_shelf: return "low_shelf";
    case amt::dsp::EqShape::peak: return "peak";
    case amt::dsp::EqShape::high_shelf: return "high_shelf";
    case amt::dsp::EqShape::high_pass: return "high_pass";
    case amt::dsp::EqShape::low_pass: return "low_pass";
  }
  return "unknown";
}

void write_params(std::ostringstream& stream, const amt::dsp::ProcessorParams& params) {
  std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    stream << std::fixed << std::setprecision(4);
    if constexpr (std::is_same_v<T, amt::dsp::GainParams>) {
      stream << "{\"gain_db\":" << value.gain_db << '}';
    } else if constexpr (std::is_same_v<T, amt::dsp::EqParams>) {
      stream << "{\"bands\":[";
      for (std::size_t index = 0; index < value.bands.size(); ++index) {
        if (index != 0U) stream << ',';
        const auto& band = value.bands[index];
        stream << "{\"shape\":\"" << eq_shape_name(band.shape)
               << "\",\"frequency_hz\":" << band.frequency_hz
               << ",\"gain_db\":" << band.gain_db << ",\"q\":" << band.q << '}';
      }
      stream << "]}";
    } else if constexpr (std::is_same_v<T, amt::dsp::CompressorParams>) {
      stream << "{\"threshold_db\":" << value.threshold_db << ",\"ratio\":" << value.ratio
             << ",\"attack_ms\":" << value.attack_ms << ",\"release_ms\":" << value.release_ms
             << ",\"knee_db\":" << value.knee_db << ",\"makeup_db\":" << value.makeup_db
             << ",\"mix\":" << value.mix << '}';
    } else if constexpr (std::is_same_v<T, amt::dsp::DynamicEqParams>) {
      stream << "{\"frequency_hz\":" << value.frequency_hz << ",\"q\":" << value.q
             << ",\"threshold_db\":" << value.threshold_db << ",\"ratio\":" << value.ratio
             << ",\"attack_ms\":" << value.attack_ms << ",\"release_ms\":" << value.release_ms
             << ",\"max_reduction_db\":" << value.max_reduction_db << '}';
    } else if constexpr (std::is_same_v<T, amt::dsp::MultibandParams>) {
      stream << "{\"low_crossover_hz\":" << value.low_crossover_hz
             << ",\"high_crossover_hz\":" << value.high_crossover_hz
             << ",\"low_threshold_db\":" << value.low_threshold_db
             << ",\"mid_threshold_db\":" << value.mid_threshold_db
             << ",\"high_threshold_db\":" << value.high_threshold_db
             << ",\"low_ratio\":" << value.low_ratio << ",\"mid_ratio\":" << value.mid_ratio
             << ",\"high_ratio\":" << value.high_ratio << '}';
    } else if constexpr (std::is_same_v<T, amt::dsp::TransientParams>) {
      stream << "{\"attack_db\":" << value.attack_db << ",\"sustain_db\":" << value.sustain_db
             << ",\"fast_ms\":" << value.fast_ms << ",\"slow_ms\":" << value.slow_ms
             << ",\"mix\":" << value.mix << '}';
    } else if constexpr (std::is_same_v<T, amt::dsp::SaturationParams>) {
      stream << "{\"drive_db\":" << value.drive_db << ",\"mix\":" << value.mix << '}';
    } else if constexpr (std::is_same_v<T, amt::dsp::StereoParams>) {
      stream << "{\"width\":" << value.width << ",\"bass_mono_hz\":" << value.bass_mono_hz << '}';
    } else if constexpr (std::is_same_v<T, amt::dsp::ClipperParams>) {
      stream << "{\"threshold_db\":" << value.threshold_db << ",\"softness\":" << value.softness << '}';
    } else if constexpr (std::is_same_v<T, amt::dsp::LimiterParams>) {
      stream << "{\"ceiling_db\":" << value.ceiling_db << ",\"release_ms\":" << value.release_ms << '}';
    }
  }, params);
}

}  // namespace

void ProcessingGraph::add(amt::dsp::ProcessorSpec spec) { nodes_.push_back(std::move(spec)); }

bool ProcessingGraph::contains(const std::string& id) const noexcept {
  return std::any_of(nodes_.begin(), nodes_.end(), [&](const auto& node) { return node.id == id; });
}

bool ProcessingGraph::set_bypass(const std::string& id, const bool bypass) noexcept {
  const auto iterator = std::find_if(nodes_.begin(), nodes_.end(),
                                     [&](const auto& node) { return node.id == id; });
  if (iterator == nodes_.end()) return false;
  iterator->bypass = bypass;
  return true;
}

bool ProcessingGraph::validate(std::string& error) const {
  if (nodes_.empty()) {
    error = "processing graph cannot be empty";
    return false;
  }
  std::set<std::string> ids;
  for (const auto& node : nodes_) {
    if (!amt::dsp::validate_processor_spec(node, error)) return false;
    if (!ids.insert(node.id).second) {
      error = "duplicate processor id: " + node.id;
      return false;
    }
  }
  return true;
}

std::string ProcessingGraph::to_json() const {
  std::ostringstream stream;
  stream << "{\"schema_version\":1,\"nodes\":[";
  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    if (index != 0U) stream << ',';
    const auto& node = nodes_[index];
    stream << "{\"id\":\"" << escape_json(node.id) << "\",\"type\":\""
           << amt::dsp::processor_type_name(node.params) << "\",\"bypass\":"
           << (node.bypass ? "true" : "false") << ",\"params\":";
    write_params(stream, node.params);
    stream << '}';
  }
  stream << "]}";
  return stream.str();
}

ProcessingGraphRuntime::ProcessingGraphRuntime(
    const ProcessingGraph& graph, const int sample_rate, const std::size_t channels)
    : sample_rate_(sample_rate), channels_(channels) {
  if (sample_rate_ <= 0 || channels_ == 0U) throw std::invalid_argument("invalid graph runtime format");
  std::string error;
  if (!graph.validate(error)) throw std::invalid_argument(error);
  for (const auto& spec : graph.nodes()) {
    auto processor = amt::dsp::make_processor(spec);
    processor->reset(sample_rate_, channels_);
    nodes_.push_back({.bypass = spec.bypass, .processor = std::move(processor)});
  }
}

void ProcessingGraphRuntime::reset() {
  for (auto& node : nodes_) node.processor->reset(sample_rate_, channels_);
}

void ProcessingGraphRuntime::process(amt::audio::AudioBuffer& buffer) {
  if (buffer.channels() != channels_) throw std::invalid_argument("processing graph channel mismatch");
  for (auto& node : nodes_) {
    if (!node.bypass) node.processor->process(buffer);
  }
}

}  // namespace amt::mastering
