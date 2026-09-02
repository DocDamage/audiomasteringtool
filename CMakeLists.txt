cmake_minimum_required(VERSION 3.24)
project(AudioMasteringTool VERSION 1.0.0 LANGUAGES C CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AMT_BUILD_TESTS "Build tests" ON)
option(AMT_BUILD_BENCHMARKS "Build benchmark harness" ON)
option(AMT_WITH_ONNX "Enable production ONNX Runtime integration in the isolated worker" OFF)
option(AMT_AUTO_FETCH_ONNX_RUNTIME
  "Fetch the pinned Windows x64 ONNX Runtime package when ONNXRUNTIME_ROOT is unset" OFF)
option(AMT_ORT_ENABLE_CUDA "Compile the optional ONNX Runtime CUDA worker execution-provider path" OFF)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Extracted ONNX Runtime package root used when AMT_WITH_ONNX=ON")

if(AMT_WITH_ONNX AND NOT ONNXRUNTIME_ROOT)
  if(AMT_AUTO_FETCH_ONNX_RUNTIME AND WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(AMT_ORT_VERSION "1.26.0")
    set(AMT_ORT_PACKAGE "onnxruntime-win-x64-${AMT_ORT_VERSION}")
    set(AMT_ORT_URL
      "https://github.com/microsoft/onnxruntime/releases/download/v${AMT_ORT_VERSION}/${AMT_ORT_PACKAGE}.zip")
    set(AMT_ORT_SHA256
      "6ebe99b5564bf4d029b6e93eac9ff423682b6212eade769e9ca3f685eaf500b4")
    set(AMT_ORT_DEPS_DIR "${CMAKE_BINARY_DIR}/_deps")
    set(AMT_ORT_ARCHIVE "${AMT_ORT_DEPS_DIR}/${AMT_ORT_PACKAGE}.zip")
    set(AMT_ORT_EXTRACTED_ROOT "${AMT_ORT_DEPS_DIR}/${AMT_ORT_PACKAGE}")

    if(NOT EXISTS "${AMT_ORT_EXTRACTED_ROOT}/include/onnxruntime_cxx_api.h")
      file(MAKE_DIRECTORY "${AMT_ORT_DEPS_DIR}")
      file(DOWNLOAD
        "${AMT_ORT_URL}"
        "${AMT_ORT_ARCHIVE}"
        EXPECTED_HASH "SHA256=${AMT_ORT_SHA256}"
        TLS_VERIFY ON
        SHOW_PROGRESS
        STATUS AMT_ORT_DOWNLOAD_STATUS)
      list(GET AMT_ORT_DOWNLOAD_STATUS 0 AMT_ORT_DOWNLOAD_CODE)
      list(GET AMT_ORT_DOWNLOAD_STATUS 1 AMT_ORT_DOWNLOAD_MESSAGE)
      if(NOT AMT_ORT_DOWNLOAD_CODE EQUAL 0)
        message(FATAL_ERROR
          "Unable to download pinned ONNX Runtime ${AMT_ORT_VERSION}: ${AMT_ORT_DOWNLOAD_MESSAGE}")
      endif()
      file(ARCHIVE_EXTRACT
        INPUT "${AMT_ORT_ARCHIVE}"
        DESTINATION "${AMT_ORT_DEPS_DIR}")
    endif()

    if(NOT EXISTS "${AMT_ORT_EXTRACTED_ROOT}/include/onnxruntime_cxx_api.h")
      message(FATAL_ERROR
        "Pinned ONNX Runtime package extracted without the expected C++ API header")
    endif()
    set(ONNXRUNTIME_ROOT "${AMT_ORT_EXTRACTED_ROOT}" CACHE PATH
      "Extracted ONNX Runtime package root used when AMT_WITH_ONNX=ON" FORCE)
  else()
    message(FATAL_ERROR
      "AMT_WITH_ONNX=ON requires ONNXRUNTIME_ROOT, or AMT_AUTO_FETCH_ONNX_RUNTIME=ON on Windows x64")
  endif()
endif()

add_subdirectory(src/core)
add_subdirectory(src/audio)
add_subdirectory(src/codec)
add_subdirectory(src/instruments)
add_subdirectory(src/interactions)
add_subdirectory(src/repair)
add_subdirectory(src/analysis)
add_subdirectory(src/separation)
add_subdirectory(src/dsp)
add_subdirectory(src/mastering)
add_subdirectory(src/decision)
add_subdirectory(src/reference)
add_subdirectory(src/revision)
add_subdirectory(src/translation)
add_subdirectory(src/preferences)
add_subdirectory(src/batch)
add_subdirectory(src/settings)
add_subdirectory(src/project)
add_subdirectory(src/playback)
add_subdirectory(src/worker)
add_subdirectory(src/cli)
add_subdirectory(src/app)

if(AMT_BUILD_BENCHMARKS)
  add_subdirectory(src/bench)
endif()

if(AMT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
