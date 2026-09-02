add_executable(amt_worker
  main.cpp
  OnnxSeparationWorker.cpp
)
target_link_libraries(amt_worker PRIVATE
  amt_core
  amt_audio
  amt_codec
)

if(AMT_WITH_ONNX)
  if(NOT ONNXRUNTIME_ROOT)
    message(FATAL_ERROR "AMT_WITH_ONNX=ON requires ONNXRUNTIME_ROOT to point to an extracted ONNX Runtime package")
  endif()

  find_path(AMT_ORT_INCLUDE_DIR onnxruntime_cxx_api.h
    PATHS "${ONNXRUNTIME_ROOT}/include"
    NO_DEFAULT_PATH
    REQUIRED)
  find_library(AMT_ORT_LIBRARY NAMES onnxruntime
    PATHS "${ONNXRUNTIME_ROOT}/lib"
    NO_DEFAULT_PATH
    REQUIRED)

  target_include_directories(amt_worker PRIVATE "${AMT_ORT_INCLUDE_DIR}")
  target_link_libraries(amt_worker PRIVATE "${AMT_ORT_LIBRARY}")
  target_compile_definitions(amt_worker PRIVATE AMT_WITH_ONNX=1)

  if(WIN32)
    file(GLOB AMT_WORKER_ORT_RUNTIME_DLLS
      "${ONNXRUNTIME_ROOT}/bin/*.dll"
      "${ONNXRUNTIME_ROOT}/lib/*.dll")
    foreach(AMT_WORKER_ORT_RUNTIME_DLL IN LISTS AMT_WORKER_ORT_RUNTIME_DLLS)
      add_custom_command(TARGET amt_worker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${AMT_WORKER_ORT_RUNTIME_DLL}"
          "$<TARGET_FILE_DIR:amt_worker>"
        VERBATIM)
    endforeach()
  endif()

  if(AMT_ORT_ENABLE_CUDA)
    target_compile_definitions(amt_worker PRIVATE AMT_ORT_ENABLE_CUDA=1)
  endif()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_worker)
