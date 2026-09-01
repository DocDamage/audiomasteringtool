add_library(amt_separation STATIC
  src/Separation.cpp
)

target_include_directories(amt_separation PUBLIC include)
target_link_libraries(amt_separation PUBLIC amt_core)

include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_separation)
