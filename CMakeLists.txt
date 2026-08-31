cmake_minimum_required(VERSION 3.24)
project(AudioMasteringTool VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AMT_BUILD_TESTS "Build tests" ON)
option(AMT_WITH_ONNX "Enable ONNX Runtime integration" OFF)

add_subdirectory(src/core)
add_subdirectory(src/worker)
add_subdirectory(src/app)

if(AMT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
