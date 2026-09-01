add_library(amt_decision STATIC
  src/Diagnosis.cpp
  src/CandidateGenerator.cpp
  src/CandidateScorer.cpp
  src/CandidateRanker.cpp
  src/ExplanationGenerator.cpp
)
target_include_directories(amt_decision PUBLIC include)
target_link_libraries(amt_decision PUBLIC amt_core amt_mastering amt_analysis amt_instruments amt_interactions)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_decision)
