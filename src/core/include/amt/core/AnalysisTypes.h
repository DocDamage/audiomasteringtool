#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace amt::core {

struct InstrumentDetection {
  std::string label;
  std::string family;
  float confidence{0.0F};
  double start_seconds{0.0};
  double end_seconds{0.0};
};

struct TrackAnalysisSummary {
  std::vector<InstrumentDetection> instruments;
  float tonal_balance_score{0.0F};
  float dynamics_score{0.0F};
  float low_end_score{0.0F};
  float transient_score{0.0F};
  float stereo_score{0.0F};
  float clarity_score{0.0F};
  float translation_score{0.0F};
  float loudness_score{0.0F};
};

bool is_valid_confidence(float value) noexcept;

}
