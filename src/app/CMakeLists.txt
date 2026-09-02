add_library(amt_app STATIC
  src/DesktopApplication.cpp
  src/MasteringSessionController.cpp
)
target_include_directories(amt_app PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(amt_app PUBLIC
  amt_core
  amt_codec
  amt_analysis
  amt_mastering
  amt_playback
  amt_project
  amt_revision
  amt_translation
  amt_preferences
  amt_batch
  amt_settings
)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_app)

add_executable(audiomasteringtool WIN32 main.cpp)
target_link_libraries(audiomasteringtool PRIVATE amt_core amt_app)
amt_enable_warnings(audiomasteringtool)


if(WIN32)
  target_sources(audiomasteringtool PRIVATE
    windows/AudioMasteringTool.rc
  )
  target_link_libraries(audiomasteringtool PRIVATE
    amt_codec
    amt_analysis
    amt_mastering
    amt_playback
    amt_project
    comctl32
    comdlg32
    shell32
  )
  target_compile_definitions(audiomasteringtool PRIVATE UNICODE _UNICODE)
  if(MSVC)
    target_link_options(audiomasteringtool PRIVATE
      "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/windows/AudioMasteringTool.manifest")
  endif()

  add_dependencies(audiomasteringtool amt_worker)
  add_custom_command(TARGET audiomasteringtool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:amt_worker>"
      "$<TARGET_FILE_DIR:audiomasteringtool>/amt_worker.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/models"
      "$<TARGET_FILE_DIR:audiomasteringtool>/models"
    VERBATIM)

  if(AMT_WITH_ONNX)
    file(GLOB AMT_ORT_RUNTIME_DLLS
      "${ONNXRUNTIME_ROOT}/bin/*.dll"
      "${ONNXRUNTIME_ROOT}/lib/*.dll")
    foreach(AMT_ORT_RUNTIME_DLL IN LISTS AMT_ORT_RUNTIME_DLLS)
      add_custom_command(TARGET audiomasteringtool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${AMT_ORT_RUNTIME_DLL}"
          "$<TARGET_FILE_DIR:audiomasteringtool>"
        VERBATIM)
    endforeach()
  endif()
endif()
