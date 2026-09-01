include(FetchContent)

# libebur128 1.2.6 predates CMake 4's removal of very old policy compatibility.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_INTERNAL_QUEUE_H ON CACHE BOOL "" FORCE)
set(WITH_STATIC_PIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  libebur128
  GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
  GIT_TAG 67b33abe1558160ed76ada1322329b0e9e058b02
  GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(libebur128)

add_library(amt_analysis STATIC
  src/LoudnessMeter.cpp
  src/SpectrumAnalyzer.cpp
  src/StereoAnalyzer.cpp
  src/IntegrityAnalyzer.cpp
  src/FileAnalyzer.cpp
  src/StructuralAnalyzer.cpp
  src/PerceptualAnalyzer.cpp
  src/CharacterAnalyzer.cpp
  src/DeepAnalysis.cpp
)
target_include_directories(amt_analysis PUBLIC include PRIVATE ${libebur128_SOURCE_DIR}/ebur128)
target_link_libraries(amt_analysis PUBLIC amt_audio amt_codec amt_instruments PRIVATE ebur128)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_analysis)
