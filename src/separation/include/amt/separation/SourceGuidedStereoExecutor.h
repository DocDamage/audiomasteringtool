#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "amt/audio/AudioBuffer.h"
#include "amt/codec/AudioIO.h"
#include "amt/separation/SourceControlEnvelope.h"

namespace amt::separation {

struct SourceGuidedStereoExecutorConfig {
  double maximum_gain_ride_db{1.5};
  double maximum_dynamic_eq_cut_db{2.5};
  double maximum_width_reduction{0.30};
  double maximum_transient_taming{0.35};
  double transient_fast_attack_ms{0.5};
  double transient_fast_release_ms{15.0};
  double transient_reference_attack_ms{12.0};
  double transient_reference_release_ms{120.0};
};

struct SourceGuidedStereoRenderResult {
  std::filesystem::path output_path;
  int sample_rate{0};
  int channels{0};
  std::int64_t frames{0};
  bool canonical_program_path{true};
  std::size_t applied_bindings{0U};
};

class SourceGuidedStereoExecutor {
 public:
  SourceGuidedStereoExecutor();
  ~SourceGuidedStereoExecutor();
  SourceGuidedStereoExecutor(SourceGuidedStereoExecutor&&) noexcept;
  SourceGuidedStereoExecutor& operator=(SourceGuidedStereoExecutor&&) noexcept;
  SourceGuidedStereoExecutor(const SourceGuidedStereoExecutor&) = delete;
  SourceGuidedStereoExecutor& operator=(const SourceGuidedStereoExecutor&) = delete;

  bool initialize(const SourceGuidedControlPlan& plan,
                  int program_sample_rate,
                  std::size_t program_channels,
                  std::string& error,
                  const SourceGuidedStereoExecutorConfig& config = {});

  bool process(amt::audio::AudioBuffer& program, std::string& error);

  [[nodiscard]] std::int64_t frames_processed() const noexcept;
  [[nodiscard]] std::size_t applicable_bindings() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::optional<SourceGuidedStereoRenderResult> render_source_guided_stereo(
    amt::codec::ICodecService& codecs,
    const std::filesystem::path& canonical_input,
    const std::filesystem::path& output,
    const SourceGuidedControlPlan& plan,
    std::string& error,
    const SourceGuidedStereoExecutorConfig& config = {},
    const amt::core::CancellationToken* cancellation = nullptr,
    const amt::core::ProgressCallback& progress = {});

}  // namespace amt::separation
