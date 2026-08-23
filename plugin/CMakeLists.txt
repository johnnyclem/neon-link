# NEON LINK VST3 — standalone JUCE project. Not an ESP-IDF component.
#
#   cmake -S plugin -B build-plugin -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build-plugin -j
#
# idf.py never sees this directory.

cmake_minimum_required(VERSION 3.22)
project(neon_link_plugin VERSION 0.2.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Apple Silicon only (K16)")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")

option(NEON_COPY_PLUGIN
  "Copy the VST3 into ~/Library/Audio/Plug-Ins/VST3 after build" ON)
if(DEFINED ENV{CI})
  set(NEON_COPY_PLUGIN OFF)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/neon_hal neon_hal)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/neon_core neon_core)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/neon_sync neon_sync)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/client neon_client)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Juce.cmake)

juce_add_plugin(NeonLink
  VERSION 0.2.5
  COMPANY_NAME "Tulip"
  BUNDLE_ID com.tulipcc.neon-link
  PLUGIN_MANUFACTURER_CODE Neon
  PLUGIN_CODE Link
  FORMATS VST3
  PRODUCT_NAME "NEON LINK"
  PLUGIN_NAME "NEON LINK"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD ${NEON_COPY_PLUGIN}
  VST3_CATEGORIES Fx
)

target_sources(NeonLink PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/DeviceController.cpp
  Source/MicController.cpp
  Source/ui/HeroTempo.cpp
  Source/ui/PhaseBar.cpp
  Source/ui/Form.cpp
  Source/ui/Pages.cpp
  Source/ui/MicPage.cpp
)

target_compile_definitions(NeonLink
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
)

set_source_files_properties(
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/DeviceController.cpp
  Source/MicController.cpp
  Source/ui/HeroTempo.cpp
  Source/ui/PhaseBar.cpp
  Source/ui/Form.cpp
  Source/ui/Pages.cpp
  Source/ui/MicPage.cpp
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")

target_link_libraries(NeonLink
  PRIVATE
    juce::juce_audio_utils
    neon_client
    neon_core
    neon_hal
    neon_sync
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)
