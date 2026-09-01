add_library(amt_separation STATIC
  src/Separation.cpp
  src/SourceGuidance.cpp
  src/ReconstructionArtifactEvaluator.cpp
  src/SourceGuidedProcessing.cpp
  src/SourceControlEnvelope.cpp
  src/SourceIssueInference.cpp
  src/SourceGuidedWorkflow.cpp
  src/WorkerSeparationProvider.cpp
  src/SourceGuidedStereoExecutor.cpp
  src/SourceGuidedStereoRenderer.cpp
)

target_include_directories(amt_separation PUBLIC include)
target_link_libraries(amt_separation PUBLIC
  amt_core
  amt_audio
  amt_codec
  amt_analysis
)

include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_separation)
