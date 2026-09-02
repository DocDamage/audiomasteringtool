add_library(amt_preferences STATIC
  src/PreferenceVector.cpp
  src/PreferenceStore.cpp
  src/PreferenceModel.cpp
  src/ProfilePreferences.cpp
)

target_include_directories(amt_preferences
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(amt_preferences
  PUBLIC
    amt_core
)
