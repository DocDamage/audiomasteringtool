#include "amt/revision/PlanEditor.h"

#include <algorithm>
#include <cmath>
#include "amt/dsp/Processors.h"
#include "amt/revision/ConstraintResolver.h"

namespace amt::revision {

PlanEditResult PlanEditor::apply_revision(
    const amt::mastering::MasteringCandidatePlan& base_plan,
    const RevisionIntent& intent) {
  PlanEditResult result;
  result.revised_plan = base_plan;
  result.revised_plan.id = base_plan.id + "_rev";
  result.revised_plan.name = base_plan.name + " (Revised)";

  auto validation = ConstraintResolver::validate_constraints(intent);
  if (!validation.is_valid) {
    result.success = false;
    result.error = "Revision rejected: " + validation.violated_constraints.front();
    return result;
  }

  amt::mastering::ProcessingGraph new_graph;

  for (const auto& node : base_plan.graph.nodes()) {
    amt::dsp::ProcessorSpec spec = node;

    for (const auto& action : intent.actions) {
      if (std::holds_alternative<amt::dsp::EqParams>(spec.params)) {
        auto eq = std::get<amt::dsp::EqParams>(spec.params);

        if (action.type == ActionType::adjust_bass_level) {
          double delta = (action.numeric_delta_db != 0.0)
                             ? action.numeric_delta_db
                             : (action.amount > 0 ? 1.5 : -1.5);
          for (auto& band : eq.bands) {
            if (band.frequency_hz <= 150.0) {
              band.gain_db = std::clamp(band.gain_db + delta, -6.0, 6.0);
            }
          }
          result.applied_changes.push_back("Adjusted low-frequency EQ by " + std::to_string(delta) + " dB");
        } else if (action.type == ActionType::reduce_harshness) {
          for (auto& band : eq.bands) {
            if (band.frequency_hz >= 3000.0 && band.frequency_hz <= 8000.0) {
              band.gain_db = std::clamp(band.gain_db - 1.2, -6.0, 4.0);
            }
          }
          result.applied_changes.push_back("Tamed upper-mid harshness (-1.2 dB @ 3-8 kHz)");
        } else if (action.type == ActionType::increase_brightness) {
          for (auto& band : eq.bands) {
            if (band.frequency_hz >= 10000.0) {
              band.gain_db = std::clamp(band.gain_db + 1.2, -6.0, 5.0);
            }
          }
          result.applied_changes.push_back("Added high-frequency air (+1.2 dB @ 10+ kHz)");
        } else if (action.type == ActionType::reduce_mud) {
          for (auto& band : eq.bands) {
            if (band.frequency_hz >= 200.0 && band.frequency_hz <= 400.0) {
              band.gain_db = std::clamp(band.gain_db - 1.5, -6.0, 4.0);
            }
          }
          result.applied_changes.push_back("Reduced low-mid mud (-1.5 dB @ 250 Hz)");
        }
        spec.params = eq;
      } else if (std::holds_alternative<amt::dsp::TransientParams>(spec.params)) {
        auto trans = std::get<amt::dsp::TransientParams>(spec.params);
        if (action.type == ActionType::increase_punch) {
          trans.attack_db = std::clamp(trans.attack_db + 1.5, -4.0, 6.0);
          result.applied_changes.push_back("Boosted transient attack (+1.5 dB)");
        } else if (action.type == ActionType::reduce_punch) {
          trans.attack_db = std::clamp(trans.attack_db - 1.0, -4.0, 6.0);
          result.applied_changes.push_back("Softened transient attack (-1.0 dB)");
        }
        spec.params = trans;
      } else if (std::holds_alternative<amt::dsp::StereoParams>(spec.params)) {
        auto stereo = std::get<amt::dsp::StereoParams>(spec.params);
        if (action.type == ActionType::adjust_stereo_width) {
          double delta = (action.amount > 0) ? 0.15 : -0.15;
          stereo.width = std::clamp(stereo.width + delta, 0.5, 1.6);
          result.applied_changes.push_back("Adjusted stereo width (" + std::to_string(stereo.width) + ")");
        }
        spec.params = stereo;
      } else if (std::holds_alternative<amt::dsp::SaturationParams>(spec.params)) {
        auto sat = std::get<amt::dsp::SaturationParams>(spec.params);
        if (action.type == ActionType::adjust_saturation) {
          sat.drive_db = std::clamp(sat.drive_db + 2.0, 0.0, 8.0);
          sat.mix = std::clamp(sat.mix + 0.15, 0.0, 0.6);
          result.applied_changes.push_back("Increased saturation drive (+2.0 dB, mix: " + std::to_string(sat.mix) + ")");
        }
        spec.params = sat;
      }
    }

    new_graph.add(spec);
  }

  // Handle global adjustments (target LUFS, ceiling)
  for (const auto& action : intent.actions) {
    if (action.type == ActionType::adjust_loudness) {
      double delta = (action.numeric_delta_db != 0.0)
                         ? action.numeric_delta_db
                         : (action.amount > 0 ? 1.0 : -1.0);
      result.revised_plan.target_lufs = std::clamp(result.revised_plan.target_lufs + delta, -16.0, -6.0);
      result.applied_changes.push_back("Adjusted target loudness to " + std::to_string(result.revised_plan.target_lufs) + " LUFS");
    }
  }

  result.revised_plan.graph = new_graph;
  result.revised_plan.rationale.push_back("Revised according to: " + intent.original_prompt);
  result.success = true;
  return result;
}

}  // namespace amt::revision
