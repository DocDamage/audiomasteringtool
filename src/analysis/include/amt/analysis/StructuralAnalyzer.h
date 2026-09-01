#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "amt/audio/AudioBuffer.h"

namespace amt::analysis {

struct TempoMetrics {
  double bpm{0.0};
  double confidence{0.0};
  double onset_density_per_second{0.0};
  double mean_onset_strength{0.0};
  double transient_fraction{0.0};
};

struct MacroDynamicsMetrics {
  double rms_p10_dbfs{-120.0};
  double rms_p50_dbfs{-120.0};
  double rms_p90_dbfs{-120.0};
  double macro_dynamic_range_db{0.0};
  double section_contrast_db{0.0};
  double crest_variability_db{0.0};
};

struct SectionSegment {
  double start_seconds{0.0};
  double end_seconds{0.0};
  double energy_dbfs{-120.0};
  double brightness{0.0};
  double transient_density{0.0};
  double stereo_width{0.0};
  std::string label_hint{"section"};
  double confidence{0.0};
};

struct StructuralMetrics {
  TempoMetrics tempo;
  MacroDynamicsMetrics macro_dynamics;
  std::vector<SectionSegment> sections;
  std::size_t boundary_count{0U};
};

class StructuralAnalyzer {
 public:
  StructuralAnalyzer(int sample_rate, std::size_t channels);
  ~StructuralAnalyzer();
  StructuralAnalyzer(const StructuralAnalyzer&) = delete;
  StructuralAnalyzer& operator=(const StructuralAnalyzer&) = delete;
  StructuralAnalyzer(StructuralAnalyzer&&) noexcept;
  StructuralAnalyzer& operator=(StructuralAnalyzer&&) noexcept;

  void process(const amt::audio::AudioBuffer& buffer);
  [[nodiscard]] StructuralMetrics finalize();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace amt::analysis
