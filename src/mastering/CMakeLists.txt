add_library(amt_mastering STATIC
  src/ProcessingGraph.cpp
  src/Planner.cpp
  src/OfflineRenderer.cpp
  src/Audition.cpp
  src/SourceGuidedMastering.cpp
)
target_include_directories(amt_mastering PUBLIC include)
target_link_libraries(amt_mastering PUBLIC
  amt_core
  amt_audio
  amt_codec
  amt_analysis
  amt_dsp
  amt_separation
)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_mastering)
