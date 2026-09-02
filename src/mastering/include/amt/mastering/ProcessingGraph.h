#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/dsp/Processors.h"

namespace amt::mastering {

class ProcessingGraph {
 public:
  void add(amt::dsp::ProcessorSpec spec);
  bool insert_before(const std::string& before_id,
                     amt::dsp::ProcessorSpec spec);
  [[nodiscard]] const std::vector<amt::dsp::ProcessorSpec>& nodes() const noexcept { return nodes_; }
  [[nodiscard]] bool empty() const noexcept { return nodes_.empty(); }
  [[nodiscard]] bool contains(const std::string& id) const noexcept;
  bool set_bypass(const std::string& id, bool bypass) noexcept;
  [[nodiscard]] bool validate(std::string& error) const;
  [[nodiscard]] std::string to_json() const;

 private:
  std::vector<amt::dsp::ProcessorSpec> nodes_;
};

class ProcessingGraphRuntime {
 public:
  ProcessingGraphRuntime(const ProcessingGraph& graph, int sample_rate, std::size_t channels);
  void reset();
  void process(amt::audio::AudioBuffer& buffer);

 private:
  struct RuntimeNode {
    bool bypass{false};
    std::unique_ptr<amt::dsp::IProcessor> processor;
  };
  int sample_rate_{0};
  std::size_t channels_{0};
  std::vector<RuntimeNode> nodes_;
};

}  // namespace amt::mastering
