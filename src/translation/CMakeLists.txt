add_library(amt_translation STATIC
  src/TranslationModel.cpp
  src/InstrumentSurvival.cpp
  src/TranslationScore.cpp
  src/TranslationAnalyzer.cpp
)

target_include_directories(amt_translation
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(amt_translation
  PUBLIC
    amt_core
    amt_audio
    amt_dsp
)
