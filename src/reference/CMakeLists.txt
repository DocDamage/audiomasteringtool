add_library(amt_reference STATIC
  src/ReferenceAnalysis.cpp
  src/ReferenceInfluence.cpp
  src/ReferenceStore.cpp
)
target_include_directories(amt_reference PUBLIC include)
target_link_libraries(amt_reference PUBLIC amt_core amt_audio amt_mastering amt_project)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_reference)
