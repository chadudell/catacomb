# Catacomb — Audio Unit instrument (and a standalone app for quick testing), built with JUCE.
#
#   ./build.sh            # build
#   ./build.sh install    # build, install to ~/Library/Audio/Plug-Ins/Components, run auval
#
# JUCE: pass -DJUCE_DIR=/path/to/JUCE, or it's fetched (8.0.9) at configure time.
cmake_minimum_required(VERSION 3.22)
project(Catacomb VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(JUCE_DIR)
  add_subdirectory("${JUCE_DIR}" juce)
else()
  include(FetchContent)
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
endif()

juce_add_plugin(Catacomb
  PRODUCT_NAME "Catacomb"
  COMPANY_NAME "Catacomb"
  BUNDLE_ID com.catacomb.synth
  DESCRIPTION "Parallel generative synthesizer"
  PLUGIN_MANUFACTURER_CODE Ctcb
  PLUGIN_CODE Cat1
  FORMATS AU Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD FALSE)

# ---- The web UI (ui/), zipped into the binary -------------------------------------
set(UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ui")
file(GLOB_RECURSE UI_FILES CONFIGURE_DEPENDS
  "${UI_ROOT}/*.html" "${UI_ROOT}/*.js" "${UI_ROOT}/*.css" "${UI_ROOT}/*.svg" "${UI_ROOT}/*.png" "${UI_ROOT}/*.woff2")
set(UI_REL)
foreach(f IN LISTS UI_FILES)
  file(RELATIVE_PATH r "${UI_ROOT}" "${f}")
  list(APPEND UI_REL "${r}")
endforeach()
set(UI_ZIP "${CMAKE_CURRENT_BINARY_DIR}/ui.zip")
add_custom_command(
  OUTPUT "${UI_ZIP}"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${UI_ZIP}"
  COMMAND "${CMAKE_COMMAND}" -E tar cf "${UI_ZIP}" --format=zip -- ${UI_REL}
  WORKING_DIRECTORY "${UI_ROOT}"
  DEPENDS ${UI_FILES}
  COMMENT "Zipping the web UI"
  VERBATIM)
juce_add_binary_data(CatacombUI SOURCES "${UI_ZIP}")

# ---- Sources -----------------------------------------------------------------------
set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../engine")
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS "${ENGINE_DIR}/*.cpp")
target_sources(Catacomb PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  ${ENGINE_SOURCES})
target_include_directories(Catacomb PRIVATE Source "${ENGINE_DIR}")

target_compile_definitions(Catacomb PUBLIC
  JUCE_WEB_BROWSER=1
  JUCE_USE_CURL=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Catacomb
  PRIVATE
    CatacombUI
    juce::juce_audio_utils
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
