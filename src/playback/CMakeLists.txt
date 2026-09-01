add_library(amt_playback STATIC
  src/AudioDevice.cpp
  src/Transport.cpp
  src/ComparisonTransport.cpp
)
target_include_directories(amt_playback PUBLIC include)
target_link_libraries(amt_playback PUBLIC amt_audio amt_codec)
if(WIN32)
  target_link_libraries(amt_playback PRIVATE winmm)
endif()
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_playback)
