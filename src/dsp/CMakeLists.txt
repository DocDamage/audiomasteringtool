add_library(amt_dsp STATIC
  src/Processors.cpp
)
target_include_directories(amt_dsp PUBLIC include)
target_link_libraries(amt_dsp PUBLIC amt_audio)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_dsp)
