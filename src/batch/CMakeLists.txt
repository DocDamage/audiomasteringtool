add_library(amt_batch STATIC
  src/CollectionAnalysis.cpp
  src/CohesionPlanner.cpp
  src/BatchQueue.cpp
  src/SequenceAudition.cpp
  src/BatchExport.cpp
)

target_include_directories(amt_batch
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(amt_batch
  PUBLIC
    amt_core
    amt_audio
    amt_codec
    amt_analysis
    amt_mastering
)
