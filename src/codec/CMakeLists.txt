add_library(amt_codec STATIC
  src/SndFileDynamic.cpp
  src/SndFileCodec.cpp
  src/AudioIO.cpp
)
target_include_directories(amt_codec PUBLIC include)
target_link_libraries(amt_codec PUBLIC amt_audio PRIVATE ${CMAKE_DL_LIBS})
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_codec)
