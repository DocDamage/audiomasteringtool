add_library(amt_instruments STATIC
  src/InstrumentTaxonomy.cpp
  src/InstrumentInference.cpp
  src/InstrumentEventTracker.cpp
  src/InstrumentSerialization.cpp
  src/InstrumentModelGovernance.cpp
  src/InstrumentCharacteristics.cpp
  src/SeparationAssistedDetector.cpp
)
target_include_directories(amt_instruments PUBLIC include)
target_link_libraries(amt_instruments PUBLIC amt_core amt_audio)
include(${CMAKE_SOURCE_DIR}/cmake/ProjectWarnings.cmake)
amt_enable_warnings(amt_instruments)
