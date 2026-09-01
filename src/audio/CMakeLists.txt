add_library(amt_audio STATIC
  src/AudioBuffer.cpp
  src/Resampler.cpp
  src/WaveformCache.cpp
)
target_include_directories(amt_audio PUBLIC include)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_audio)
