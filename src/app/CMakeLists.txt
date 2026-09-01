add_executable(audiomasteringtool WIN32 Phase5Main.cpp)
target_link_libraries(audiomasteringtool PRIVATE amt_core)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(audiomasteringtool)

if(WIN32)
  target_link_libraries(audiomasteringtool PRIVATE
    amt_codec
    amt_analysis
    amt_mastering
    amt_playback
    amt_project
    comctl32
    comdlg32
    shell32
  )
  target_compile_definitions(audiomasteringtool PRIVATE UNICODE _UNICODE)
endif()
