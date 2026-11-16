cmake_minimum_required(VERSION 3.22)
project(OTLAK VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OT_BUILD_PLUGIN "Build the JUCE plugin" ON)
option(OT_BUILD_TESTS "Build the headless core tests" ON)
option(OT_BUILD_SNAPSHOT "Build the UI snapshot tool (development)" OFF)
set(OT_JUCE_DIR "" CACHE PATH "Local JUCE checkout (fetched automatically when empty)")

# ---------------------------------------------------------------- core (framework independent)
add_library(ot_core STATIC core/Otlak.cpp core/FmVoice.cpp core/Sequencer.cpp core/Dice.cpp)
target_include_directories(ot_core PUBLIC core)

if(OT_BUILD_TESTS)
    enable_testing()
    add_executable(ot_tests tests/test_core.cpp)
    target_link_libraries(ot_tests PRIVATE ot_core)
    add_test(NAME ot_tests COMMAND ot_tests)
endif()

# ---------------------------------------------------------------- plugin
if(OT_BUILD_PLUGIN)
    include(FetchContent)
    if(OT_JUCE_DIR)
        add_subdirectory(${OT_JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    if(APPLE)
        set(OT_FORMATS VST3 AU Standalone)
    elseif(WIN32)
        set(OT_FORMATS VST3 Standalone)
    else()
        set(OT_FORMATS VST3)
    endif()

    juce_add_plugin(Otlak
        COMPANY_NAME "Otlak"
        BUNDLE_ID "com.otlak.fmdrums"
        PLUGIN_MANUFACTURER_CODE Otlk
        PLUGIN_CODE Otk1
        FORMATS ${OT_FORMATS}
        PRODUCT_NAME "Otlak"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Drum
        AU_MAIN_TYPE kAudioUnitType_MusicDevice)

    target_sources(Otlak PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp)

    target_compile_definitions(Otlak PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    if(UNIX AND NOT APPLE)
        target_compile_definitions(Otlak PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
    endif()

    target_link_libraries(Otlak
        PRIVATE
            ot_core
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

if(OT_BUILD_PLUGIN AND OT_BUILD_SNAPSHOT)
    add_executable(ot_snapshot tests/snapshot.cpp)
    target_include_directories(ot_snapshot PRIVATE plugin $<TARGET_PROPERTY:Otlak,INCLUDE_DIRECTORIES>)
    target_compile_definitions(ot_snapshot PRIVATE $<TARGET_PROPERTY:Otlak,COMPILE_DEFINITIONS>)
    target_link_libraries(ot_snapshot PRIVATE Otlak)
endif()
