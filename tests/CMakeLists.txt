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
