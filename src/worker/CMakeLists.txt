add_executable(amt_worker main.cpp)
target_link_libraries(amt_worker PRIVATE amt_core)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_worker)
