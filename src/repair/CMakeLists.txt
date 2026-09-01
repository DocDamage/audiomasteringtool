add_library(amt_repair STATIC
  src/RepairValidator.cpp
  src/Declipping.cpp
  src/TransientRepair.cpp
  src/Denoise.cpp
)
target_include_directories(amt_repair PUBLIC include)
target_link_libraries(amt_repair PUBLIC amt_core amt_audio)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_repair)
