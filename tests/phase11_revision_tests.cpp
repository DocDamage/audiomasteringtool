#include <cassert>
#include <iostream>
#include "amt/dsp/Processors.h"
#include "amt/mastering/Planner.h"
#include "amt/revision/ConstraintResolver.h"
#include "amt/revision/PlanEditor.h"
#include "amt/revision/RevisionExplanation.h"
#include "amt/revision/RevisionParser.h"
#include "amt/revision/TargetResolver.h"

int main() {
  std::cout << "[Phase 11] Running Natural-Language Revision Engine Tests...\n";

  amt::revision::RevisionParser parser;

  // Test 1: Parser extracts punch and harshness commands
  {
    auto intent = parser.parse("make the drums hit harder and smooth the top");
    assert(intent.parsed_successfully);
    assert(intent.actions.size() >= 2);
    assert(intent.actions[0].type == amt::revision::ActionType::increase_punch);
    assert(intent.actions[0].target == amt::revision::TargetScope::stem_drums);
    assert(intent.actions[1].type == amt::revision::ActionType::reduce_harshness);
    std::cout << "  ✓ Test 1: Parsed multi-intent prompt accurately\n";
  }

  // Test 2: Constraint extraction ("don't touch the bass")
  {
    auto intent = parser.parse("more width, don't touch the bass");
    assert(intent.parsed_successfully);
    assert(intent.constraints.size() == 1);
    assert(intent.constraints[0].target == amt::revision::TargetScope::stem_bass);
    assert(intent.actions.size() == 1);
    assert(intent.actions[0].type == amt::revision::ActionType::adjust_stereo_width);
    std::cout << "  ✓ Test 2: Extracted negative constraint 'don't touch the bass'\n";
  }

  // Test 3: Constraint validation blocks conflicting requests
  {
    amt::revision::RevisionIntent conflict_intent;
    conflict_intent.actions.push_back({
        .type = amt::revision::ActionType::adjust_bass_level,
        .target = amt::revision::TargetScope::stem_bass,
        .amount = 0.5,
        .raw_phrase = "boost bass"});
    conflict_intent.constraints.push_back({
        .target = amt::revision::TargetScope::stem_bass,
        .rule = "don't touch bass",
        .active = true});

    auto val = amt::revision::ConstraintResolver::validate_constraints(conflict_intent);
    assert(!val.is_valid);
    assert(!val.violated_constraints.empty());
    std::cout << "  ✓ Test 3: ConstraintResolver successfully blocked conflicting edits\n";
  }

  // Test 4: PlanEditor modifies candidate plan correctly
  {
    amt::mastering::MasteringCandidatePlan base_plan;
    base_plan.id = "plan_a";
    base_plan.name = "Master A";
    base_plan.target_lufs = -10.0;

    amt::dsp::EqParams eq;
    eq.bands.push_back({.shape = amt::dsp::EqShape::peak, .frequency_hz = 100.0, .gain_db = 0.0, .q = 0.707});
    eq.bands.push_back({.shape = amt::dsp::EqShape::peak, .frequency_hz = 5000.0, .gain_db = 0.0, .q = 1.0});
    base_plan.graph.add({.id = "eq_1", .params = eq});

    amt::dsp::TransientParams trans;
    trans.attack_db = 0.0;
    base_plan.graph.add({.id = "trans_1", .params = trans});

    auto intent = parser.parse("make the drums hit harder and tame hats are too sharp");
    auto res = amt::revision::PlanEditor::apply_revision(base_plan, intent);
    assert(res.success);
    assert(!res.applied_changes.empty());

    // Check explanation
    auto explanation = amt::revision::RevisionExplanation::generate_explanation(intent, res);
    assert(!explanation.empty());
    std::cout << "  ✓ Test 4: PlanEditor and ExplanationGenerator executed successfully\n";
  }

  // Test 5: TargetResolver validation
  {
    std::vector<amt::instruments::InstrumentEvent> instruments;
    amt::instruments::InstrumentEvent ev;
    ev.taxonomy_id = "bass.synth.808";
    ev.display_label = "808 Sub";
    ev.confidence = 0.95;
    instruments.push_back(ev);

    auto target = amt::revision::TargetResolver::resolve(
        amt::revision::TargetScope::instrument_808, "808", nullptr, &instruments);
    assert(target.is_present);
    assert(target.confidence > 0.9);
    std::cout << "  ✓ Test 5: TargetResolver validated 808 detection confidence\n";
  }

  std::cout << "[Phase 11] All tests passed successfully!\n";
  return 0;
}
