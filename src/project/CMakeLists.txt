add_library(amt_project STATIC
  src/ProjectStore.cpp
  src/ExportRecipes.cpp
)
target_include_directories(amt_project PUBLIC include)
target_link_libraries(amt_project PUBLIC amt_core amt_codec)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_project)
