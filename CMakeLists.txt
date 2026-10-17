cmake_minimum_required(VERSION 3.22)
# These two must be set BEFORE project(), or CMake ignores them (learned the hard way on the IPMOK plugin:
# its first CI build was arm64-only). Universal = Apple silicon + Intel; 11.0 runs on macOS 12.
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal: Apple silicon + Intel")
endif()

# The version is the plugin's version in the host. Raise it with every release: macOS and Logic cache a
# plugin's name and details and only refresh them when the installed file's version goes up.
project(AMB1 VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AMB1_ENGINE_ONLY "Build only the engine and the JUCE-free tests (no JUCE, no plugin)" OFF)

# ---------------------------------------------------------------------------
# The AMB-1 engine, vendored unmodified from upstream (see engine/UPSTREAM.md).
# -ffp-contract=off is upstream's own setting: it keeps the float maths the same on arm64 and x86_64.
# No -ffast-math.
# ---------------------------------------------------------------------------
add_library(amb_engine STATIC engine/amb.c Source/EngineHooks.cpp)
target_include_directories(amb_engine PUBLIC engine Source)
target_compile_definitions(amb_engine PRIVATE EN_HOST)
target_compile_options(amb_engine PRIVATE -ffp-contract=off)
set_target_properties(amb_engine PROPERTIES C_STANDARD 99 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)

# The same engine for the MIDI effect build: the notes it sends go to a list (MidiFxHooks.cpp) instead of nowhere.
add_library(amb_engine_fx STATIC engine/amb.c Source/MidiFxHooks.cpp)
target_include_directories(amb_engine_fx PUBLIC engine Source)
target_compile_definitions(amb_engine_fx PRIVATE EN_HOST)
target_compile_options(amb_engine_fx PRIVATE -ffp-contract=off)
set_target_properties(amb_engine_fx PROPERTIES C_STANDARD 99 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)

# The JUCE-free glue: parameter diffing, the 44.1 kHz engine, the resampler.
add_library(amb1_core STATIC Source/AmbCore.cpp Source/GardenState.cpp)
target_link_libraries(amb1_core PUBLIC amb_engine)
set_target_properties(amb1_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The same glue on top of the MIDI-hook engine (the MIDI effect build).
add_library(amb1_core_fx STATIC Source/AmbCore.cpp Source/GardenState.cpp)
target_link_libraries(amb1_core_fx PUBLIC amb_engine_fx)
set_target_properties(amb1_core_fx PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(render_test tests/render_test.cpp)
target_link_libraries(render_test PRIVATE amb_engine)

add_executable(rate_test tests/rate_test.cpp)
target_link_libraries(rate_test PRIVATE amb1_core)

# scales_test: the Scale menu's scales through the engine (it defines the engine's MIDI-out hook itself to see every note)
add_executable(scales_test tests/scales_test.cpp)
target_link_libraries(scales_test PRIVATE amb1_core)

# seed_test: the random start (en_seed) in fresh processes. It runs itself as a child process per case.
add_executable(seed_test tests/seed_test.cpp)
target_link_libraries(seed_test PRIVATE amb1_core)

# midifx_test: the engine running with no audio and handing its notes out (the MIDI effect build's engine side)
add_executable(midifx_test tests/midifx_test.cpp)
target_link_libraries(midifx_test PRIVATE amb1_core_fx)

# echo_test #includes engine/amb.c to read the engine's own state, so it is built on its own, not linked with amb_engine.
add_executable(echo_test tests/echo_test.c)
target_include_directories(echo_test PRIVATE engine)
target_compile_options(echo_test PRIVATE -ffp-contract=off)
target_link_libraries(echo_test PRIVATE m)
set_target_properties(echo_test PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)

if(AMB1_ENGINE_ONLY)
  return()
endif()

# ---------------------------------------------------------------------------
# JUCE plugin (AU + VST3)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
  set(AMB1_FORMATS AU VST3)
  set(AMB1_COPY FALSE)
else()
  set(AMB1_FORMATS VST3)
  set(AMB1_COPY FALSE)
endif()

juce_add_plugin(AMB1
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Amb1
  FORMATS ${AMB1_FORMATS}
  PRODUCT_NAME "AMB-1"
  PLUGIN_NAME "AMB-1"
  DESCRIPTION "Unofficial plugin port of the AMB-1 generative ambient engine"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD ${AMB1_COPY})

target_sources(AMB1 PRIVATE
  Source/PluginProcessor.cpp Source/PluginProcessor.h
  Source/PluginEditor.cpp Source/PluginEditor.h
  Source/GardenView.cpp Source/GardenView.h)
target_compile_definitions(AMB1 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(AMB1 PRIVATE amb1_core
  juce::juce_audio_utils juce::juce_audio_processors
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Headless test that drives the actual plugin object (processBlock, programs, state, parameter ids)
add_executable(plugin_test tests/plugin_test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/GardenView.cpp)
target_include_directories(plugin_test PRIVATE Source)
target_compile_definitions(plugin_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(plugin_test PRIVATE amb1_core juce::juce_audio_processors juce::juce_recommended_config_flags)

# ---------------------------------------------------------------------------
# "AMB-1 MIDI": the same engine and controls as a MIDI effect (AU type aumi, for Logic's MIDI FX slot above any
# instrument). No sound and no audio buses: the garden's notes leave as MIDI. Built from the same sources with
# AMB1_MIDI_FX=1, the way dotriaconta-tone is built from the IPMOK sources. Its own AU codes (Ambm), so it and the
# instrument (Amb1) are two separate plugins.
# ---------------------------------------------------------------------------
juce_add_plugin(AMB1MIDI
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Ambm
  FORMATS ${AMB1_FORMATS}
  PRODUCT_NAME "AMB-1-MIDI"   # the installed file: AMB-1-MIDI.component (no spaces, for the build scripts); the host shows PLUGIN_NAME
  PLUGIN_NAME "AMB-1 MIDI"
  DESCRIPTION "Unofficial MIDI effect port of the AMB-1 generative ambient engine"
  IS_SYNTH FALSE
  IS_MIDI_EFFECT TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
  COPY_PLUGIN_AFTER_BUILD ${AMB1_COPY})

target_sources(AMB1MIDI PRIVATE
  Source/PluginProcessor.cpp Source/PluginProcessor.h
  Source/PluginEditor.cpp Source/PluginEditor.h
  Source/GardenView.cpp Source/GardenView.h)
target_compile_definitions(AMB1MIDI PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 AMB1_MIDI_FX=1)
target_link_libraries(AMB1MIDI PRIVATE amb1_core_fx
  juce::juce_audio_utils juce::juce_audio_processors
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Headless test of the MIDI effect build (drives the real processor: MIDI in, MIDI out, no audio)
add_executable(fx_test tests/fx_test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/GardenView.cpp)
target_include_directories(fx_test PRIVATE Source)
target_compile_definitions(fx_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 AMB1_MIDI_FX=1)
target_link_libraries(fx_test PRIVATE amb1_core_fx juce::juce_audio_processors juce::juce_recommended_config_flags)
