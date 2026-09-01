add_executable(amt_phase0_tests phase0_tests.cpp)
target_link_libraries(amt_phase0_tests PRIVATE amt_core amt_codec)
add_test(NAME phase0_core COMMAND amt_phase0_tests)

add_executable(amt_phase1_tests phase1_tests.cpp)
target_link_libraries(amt_phase1_tests PRIVATE
  amt_core
  amt_audio
  amt_codec
  amt_analysis
  amt_playback
)
add_test(NAME phase1_audio_analysis COMMAND amt_phase1_tests)

add_executable(amt_phase2_tests phase2_tests.cpp)
target_link_libraries(amt_phase2_tests PRIVATE
  amt_core
  amt_audio
  amt_codec
  amt_analysis
  amt_dsp
  amt_mastering
  amt_playback
)
add_test(NAME phase2_deterministic_mastering COMMAND amt_phase2_tests)

add_executable(amt_phase3_tests phase3_tests.cpp)
target_link_libraries(amt_phase3_tests PRIVATE
  amt_core
  amt_audio
  amt_codec
  amt_analysis
)
add_test(NAME phase3_structural_perceptual_analysis COMMAND amt_phase3_tests)

add_executable(amt_phase3_planner_tests phase3_planner_tests.cpp)
target_link_libraries(amt_phase3_planner_tests PRIVATE
  amt_core
  amt_audio
  amt_codec
  amt_analysis
  amt_dsp
  amt_mastering
)
add_test(NAME phase3_analysis_aware_planner COMMAND amt_phase3_planner_tests)

add_executable(amt_phase4_tests phase4_tests.cpp)
target_link_libraries(amt_phase4_tests PRIVATE
  amt_core
  amt_codec
  amt_project
)
add_test(NAME phase4_project_history_and_export_recipes COMMAND amt_phase4_tests)

add_executable(amt_phase6_instruments_tests phase6_instruments_tests.cpp)
target_link_libraries(amt_phase6_instruments_tests PRIVATE amt_core amt_instruments)
add_test(NAME phase6_instrument_taxonomy_and_inference COMMAND amt_phase6_instruments_tests)

add_executable(amt_phase7_interactions_tests phase7_interactions_tests.cpp)
target_link_libraries(amt_phase7_interactions_tests PRIVATE amt_core amt_interactions)
add_test(NAME phase7_interaction_evidence_and_damage_guard COMMAND amt_phase7_interactions_tests)

add_executable(amt_phase5_tests phase5_tests.cpp)
target_link_libraries(amt_phase5_tests PRIVATE
  amt_core
  amt_separation
)
add_test(NAME phase5_source_guided_separation_policy COMMAND amt_phase5_tests)

add_executable(amt_phase5_fingerprint_tests phase5_fingerprint_tests.cpp)
target_link_libraries(amt_phase5_fingerprint_tests PRIVATE
  amt_core
  amt_separation
)
add_test(NAME phase5_source_fingerprint_and_cache_identity COMMAND amt_phase5_fingerprint_tests)

add_executable(amt_phase5_reconstruction_contract_tests phase5_reconstruction_contract_tests.cpp)
target_link_libraries(amt_phase5_reconstruction_contract_tests PRIVATE
  amt_core
  amt_separation
)
add_test(NAME phase5_complete_reconstruction_contract COMMAND amt_phase5_reconstruction_contract_tests)

add_executable(amt_phase5_artifact_evaluator_tests phase5_artifact_evaluator_tests.cpp)
target_link_libraries(amt_phase5_artifact_evaluator_tests PRIVATE
  amt_core
  amt_separation
)
add_test(NAME phase5_reconstruction_artifact_evaluator COMMAND amt_phase5_artifact_evaluator_tests)

add_executable(amt_phase5_source_guided_processing_tests phase5_source_guided_processing_tests.cpp)
target_link_libraries(amt_phase5_source_guided_processing_tests PRIVATE
  amt_core
  amt_separation
)
add_test(NAME phase5_source_guided_processing_plan COMMAND amt_phase5_source_guided_processing_tests)

add_executable(amt_phase5_source_control_tests phase5_source_control_tests.cpp)
target_link_libraries(amt_phase5_source_control_tests PRIVATE
  amt_core
  amt_separation
)
add_test(NAME phase5_source_control_envelopes COMMAND amt_phase5_source_control_tests)

add_executable(amt_phase5_source_issue_inference_tests phase5_source_issue_inference_tests.cpp)
target_link_libraries(amt_phase5_source_issue_inference_tests PRIVATE
  amt_core
  amt_audio
  amt_codec
  amt_analysis
  amt_dsp
  amt_mastering
  amt_separation
)
add_test(NAME phase5_source_issue_inference COMMAND amt_phase5_source_issue_inference_tests)

add_executable(amt_phase5_source_guided_workflow_tests phase5_source_guided_workflow_tests.cpp)
target_link_libraries(amt_phase5_source_guided_workflow_tests PRIVATE
  amt_core
  amt_audio
  amt_codec
  amt_analysis
  amt_dsp
  amt_mastering
  amt_separation
)
add_test(NAME phase5_source_guided_evidence_workflow
  COMMAND amt_phase5_source_guided_workflow_tests)

add_executable(amt_phase5_model_registry_tests phase5_model_registry_tests.cpp)
target_link_libraries(amt_phase5_model_registry_tests PRIVATE
  amt_core
  amt_codec
  amt_analysis
  amt_separation
)
add_test(NAME phase5_production_model_registry COMMAND amt_phase5_model_registry_tests)

add_executable(amt_phase5_model_artifact_installer_tests
  phase5_model_artifact_installer_tests.cpp)
target_link_libraries(amt_phase5_model_artifact_installer_tests PRIVATE
  amt_core
  amt_codec
  amt_analysis
  amt_separation
)
add_test(NAME phase5_trusted_model_artifact_installer
  COMMAND amt_phase5_model_artifact_installer_tests)

add_executable(amt_phase5_source_guided_executor_tests phase5_source_guided_executor_tests.cpp)
target_link_libraries(amt_phase5_source_guided_executor_tests PRIVATE
  amt_core
  amt_separation
)
add_test(NAME phase5_source_guided_stereo_executor COMMAND amt_phase5_source_guided_executor_tests)

add_executable(amt_phase5_source_guided_renderer_tests phase5_source_guided_renderer_tests.cpp)
target_link_libraries(amt_phase5_source_guided_renderer_tests PRIVATE
  amt_core
  amt_separation
)
add_test(NAME phase5_source_guided_stereo_renderer COMMAND amt_phase5_source_guided_renderer_tests)

add_executable(amt_phase5_source_guided_mastering_tests phase5_source_guided_mastering_tests.cpp)
target_link_libraries(amt_phase5_source_guided_mastering_tests PRIVATE
  amt_core
  amt_audio
  amt_codec
  amt_analysis
  amt_dsp
  amt_mastering
  amt_separation
)
add_test(NAME phase5_source_guided_mastering_integration COMMAND amt_phase5_source_guided_mastering_tests)

add_executable(amt_phase5_effective_plan_provenance_tests
  phase5_effective_plan_provenance_tests.cpp)
target_link_libraries(amt_phase5_effective_plan_provenance_tests PRIVATE
  amt_core
  amt_audio
  amt_codec
  amt_analysis
  amt_dsp
  amt_mastering
  amt_separation
)
add_test(NAME phase5_effective_mastering_plan_provenance
  COMMAND amt_phase5_effective_plan_provenance_tests)

add_executable(amt_phase6_characteristics_tests phase6_characteristics_tests.cpp)
target_link_libraries(amt_phase6_characteristics_tests PRIVATE
  amt_core
  amt_audio
  amt_instruments
)
add_test(NAME phase6_instrument_characteristics COMMAND amt_phase6_characteristics_tests)

add_executable(amt_phase7_interaction_engine_tests phase7_interaction_engine_tests.cpp)
target_link_libraries(amt_phase7_interaction_engine_tests PRIVATE
  amt_core
  amt_audio
  amt_instruments
  amt_interactions
)
add_test(NAME phase7_interaction_engine_and_trackers COMMAND amt_phase7_interaction_engine_tests)

add_executable(amt_phase8_repair_tests phase8_repair_tests.cpp)
target_link_libraries(amt_phase8_repair_tests PRIVATE
  amt_core
  amt_audio
  amt_repair
)
add_test(NAME phase8_restoration_and_repair COMMAND amt_phase8_repair_tests)

add_executable(amt_phase9_decision_tests phase9_decision_tests.cpp)
target_link_libraries(amt_phase9_decision_tests PRIVATE
  amt_core
  amt_audio
  amt_analysis
  amt_mastering
  amt_instruments
  amt_interactions
  amt_decision
)
add_test(NAME phase9_decision_engine_v2 COMMAND amt_phase9_decision_tests)

add_executable(amt_phase10_reference_tests phase10_reference_tests.cpp)
target_link_libraries(amt_phase10_reference_tests PRIVATE
  amt_core
  amt_audio
  amt_mastering
  amt_project
  amt_reference
)
add_test(NAME phase10_reference_mastering_and_my_sound COMMAND amt_phase10_reference_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME phase5_calibration_tooling
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/phase5_calibration_tooling_tests.py")
  add_test(
    NAME phase5_model_registry_python_validation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/validate_model_registry.py")
  add_test(
    NAME phase6_instrument_corpus_manifest_validation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/phase6_instrument_manifest_tests.py")
  set_tests_properties(
    phase5_calibration_tooling
    phase5_model_registry_python_validation
    PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
else()
  message(WARNING "Python 3 not found; Phase 5 Python tooling tests will be skipped")
endif()
