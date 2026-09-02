add_library(amt_codec STATIC
  src/SndFileDynamic.cpp
  src/SndFileCodec.cpp
  src/MediaFoundationCodec.cpp
  src/AudioIO.cpp
)
target_include_directories(amt_codec PUBLIC include)
target_link_libraries(amt_codec PUBLIC amt_audio amt_core PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(amt_codec PRIVATE mfplat mfreadwrite mfuuid propsys)
endif()
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_codec)
