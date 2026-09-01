#include "amt/repair/RepairValidator.h"

#include <algorithm>
#include <cmath>

namespace amt::repair {

std::string issue_type_name(IssueType type) {
  switch (type) {
    case IssueType::none: return "None";
    case IssueType::peak_clipping: return "Peak Clipping";
    case IssueType::transient_smearing: return "Transient Smearing";
    case IssueType::low_end_mud: return "Low-End Mud";
    case IssueType::high_frequency_harshness: return "High Frequency Harshness";
    case IssueType::background_noise_floor: return "Background Noise Floor";
    case IssueType::stereo_phase_cancellation: return "Stereo Phase Cancellation";
  }
  return "Unknown";
}

std::string repair_type_name(RepairType type) {
  switch (type) {
    case RepairType::none: return "None";
    case RepairType::declipping: return "Declipping";
    case RepairType::transient_reconstruction: return "Transient Reconstruction";
    case RepairType::localized_spectral_denoise: return "Localized Spectral Denoise";
    case RepairType::low_band_mono_stabilizer: return "Low-Band Mono Stabilizer";
    case RepairType::dynamic_resonance_suppression: return "Dynamic Resonance Suppression";
  }
  return "Unknown";
}

ValidationResult RepairValidator::validate(
    const amt::audio::AudioBuffer& original,
    const amt::audio::AudioBuffer& repaired,
    const RepairPolicy& policy) const {
  ValidationResult res{};
  if (original.frames() != repaired.frames() || original.channels() != repaired.channels() ||
      original.frames() == 0) {
    res.passed = false;
    res.reasons.push_back("Frame count or channel count mismatch");
    return res;
  }

  double orig_peak_sq = 0.0;
  double rep_peak_sq = 0.0;
  double diff_energy = 0.0;
  double orig_energy = 0.0;

  const std::size_t frames = original.frames();
  const std::size_t channels = original.channels();

  for (std::size_t c = 0; c < channels; ++c) {
    const float* orig_ptr = original.channel(c).data();
    const float* rep_ptr = repaired.channel(c).data();
    for (std::size_t i = 0; i < frames; ++i) {
      const double o = static_cast<double>(orig_ptr[i]);
      const double r = static_cast<double>(rep_ptr[i]);
      if (o * o > orig_peak_sq) orig_peak_sq = o * o;
      if (r * r > rep_peak_sq) rep_peak_sq = r * r;
      diff_energy += (r - o) * (r - o);
      orig_energy += o * o;
    }
  }

  const double orig_peak = std::sqrt(orig_peak_sq);
  const double rep_peak = std::sqrt(rep_peak_sq);

  if (orig_peak > 1e-9 && rep_peak > 1e-9) {
    res.peak_delta_dbtp = 20.0 * std::log10(rep_peak / orig_peak);
  }

  if (orig_energy > 1e-12) {
    res.spectral_difference = std::sqrt(diff_energy / orig_energy);
  }

  res.passed = true;
  if (res.spectral_difference > policy.max_spectral_side_effect_threshold) {
    res.passed = false;
    res.reasons.push_back("Spectral difference exceeds damage threshold");
  }
  if (res.peak_delta_dbtp > policy.max_declipping_gain_db) {
    res.passed = false;
    res.reasons.push_back("Peak level increase exceeds safety limit");
  }

  return res;
}

}  // namespace amt::repair
