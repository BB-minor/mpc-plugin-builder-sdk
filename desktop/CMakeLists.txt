# Drum Infurno - desktop VST3 / AU build.
#
# SPDX-License-Identifier: MIT
#
# This wraps core/ (infurno_core), the same DSP the MPC plugin runs. There is no DSP in
# this directory: PluginProcessor only translates between JUCE and the engine.
#
#   cmake -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# JUCE is fetched at configure time; nothing needs to be installed first beyond CMake,
# git and the Xcode command line tools. See README.md in this folder for the full
# sequence including pluginval and auval.

cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect.
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    # Universal by default so one build runs on Apple Silicon and Intel. Override with
    # -DCMAKE_OSX_ARCHITECTURES=arm64 for a faster local-only build.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

project(DrumInfurno VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(INFURNO_COPY_AFTER_BUILD "Install the built plugin into the user plugin folders" ON)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15          # pinned deliberately; bump only on purpose
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# The shared DSP core (../core) - the same library the MPC build compiles
# ---------------------------------------------------------------------------
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_CURRENT_BINARY_DIR}/infurno_core)

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
juce_add_plugin(DrumInfurno
    COMPANY_NAME                "INFUR"
    BUNDLE_ID                   "com.infur.druminfurno"
    PLUGIN_MANUFACTURER_CODE    Infr          # 4 chars, at least one uppercase
    PLUGIN_CODE                 Dinf          # 4 chars, unique to this plugin
    FORMATS                     AU VST3
    PRODUCT_NAME                "Drum Infurno"
    DESCRIPTION                 "Drum colouration: Damage, Cut and a dynamic modal Body"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Distortion
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     ${INFURNO_COPY_AFTER_BUILD}
)

target_sources(DrumInfurno PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(DrumInfurno PUBLIC
    JUCE_WEB_BROWSER=0            # not needed, and avoids a pile of system dependencies
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(DrumInfurno
    PRIVATE
        infurno_core              # <- the one and only copy of the DSP
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
