cmake_minimum_required(VERSION 3.24)
project(amt_onnx_spike LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ONNXRUNTIME_ROOT "" CACHE PATH "Extracted ONNX Runtime package root")
option(AMT_ORT_ENABLE_CUDA "Compile CUDA execution-provider path" OFF)
if(NOT ONNXRUNTIME_ROOT)
  message(FATAL_ERROR "Set ONNXRUNTIME_ROOT to an extracted ONNX Runtime package")
endif()

find_path(ORT_INCLUDE_DIR onnxruntime_cxx_api.h PATHS "${ONNXRUNTIME_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
find_library(ORT_LIBRARY NAMES onnxruntime PATHS "${ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)

add_executable(amt_onnx_spike main.cpp)
target_include_directories(amt_onnx_spike PRIVATE "${ORT_INCLUDE_DIR}")
target_link_libraries(amt_onnx_spike PRIVATE "${ORT_LIBRARY}")

if(AMT_ORT_ENABLE_CUDA)
  target_compile_definitions(amt_onnx_spike PRIVATE AMT_ORT_ENABLE_CUDA=1)
endif()
