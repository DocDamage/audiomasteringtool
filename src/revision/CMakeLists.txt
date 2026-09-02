add_library(amt_revision STATIC
  src/RevisionParser.cpp
  src/TargetResolver.cpp
  src/ConstraintResolver.cpp
  src/PlanEditor.cpp
  src/RevisionExplanation.cpp
)

target_include_directories(amt_revision
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(amt_revision
  PUBLIC
    amt_core
    amt_audio
    amt_dsp
    amt_mastering
    amt_instruments
)
