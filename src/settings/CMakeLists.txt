add_library(amt_settings STATIC
  src/SettingsManager.cpp
  src/CacheManager.cpp
  src/ModelManager.cpp
  src/CrashReporting.cpp
)

target_include_directories(amt_settings
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(amt_settings
  PUBLIC
    amt_core
)
