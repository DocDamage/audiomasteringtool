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
