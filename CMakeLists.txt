cmake_minimum_required(VERSION 3.22)

project(TuneForge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(TuneForge
    COMPANY_NAME                "TuneForge"
    PRODUCT_NAME                "TuneForge"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE    Tfrg
    PLUGIN_CODE                 Tfp1
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             Fx
    COPY_PLUGIN_AFTER_BUILD     FALSE)

juce_generate_juce_header(TuneForge)

target_sources(TuneForge PRIVATE
    Source/PitchCorrector.h
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp)

target_compile_definitions(TuneForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TuneForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Standalone engine test (no JUCE needed):  cmake --build build --target engine_test
add_executable(engine_test tests/test_engine.cpp)
target_include_directories(engine_test PRIVATE Source)
set_target_properties(engine_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
