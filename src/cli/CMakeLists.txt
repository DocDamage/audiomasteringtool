add_executable(amt_cli main.cpp)
target_link_libraries(amt_cli PRIVATE amt_core amt_codec)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_cli)
