add_library(amt_core STATIC
  src/Version.cpp
  src/AnalysisTypes.cpp
  src/InferenceBackend.cpp
)
target_include_directories(amt_core PUBLIC include)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_core)

if(AMT_WITH_ONNX)
  target_compile_definitions(amt_core PUBLIC AMT_WITH_ONNX=1)
endif()
