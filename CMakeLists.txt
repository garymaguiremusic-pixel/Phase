cmake_minimum_required(VERSION 3.22)

project(PhaseCheck VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Static runtime so the .vst3 has no dependency on the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(PHASECHECK_BUILD_AS_INSTRUMENT
       "Register the plug-in as an instrument instead of an audio effect (see README)" OFF)
option(PHASECHECK_COPY_PLUGIN
       "Copy the VST3 into the system plug-in folder after building (Windows: needs an Administrator prompt)" OFF)
option(PHASECHECK_BUILD_TESTS "Build the unit-test console app" ON)

# ---------------------------------------------------------------------------
# JUCE 8 via FetchContent
#   Offline / faster builds: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Embedded fonts (SIL Open Font License, licence files in Resources/Fonts)
# ---------------------------------------------------------------------------
juce_add_binary_data(PhaseCheckFonts
    NAMESPACE PhaseCheckBinary
    SOURCES
        Resources/Fonts/Inter-Regular.ttf
        Resources/Fonts/Inter-Medium.ttf
        Resources/Fonts/Inter-SemiBold.ttf
        Resources/Fonts/Inter-Bold.ttf
        Resources/Fonts/JetBrainsMono-Regular.ttf
        Resources/Fonts/JetBrainsMono-SemiBold.ttf)

# DSP + analysis sources shared by the plug-in and the tests
set(PHASECHECK_CORE_SOURCES
    Source/dsp/HilbertRotator.cpp
    Source/dsp/KickProcessor.cpp
    Source/analysis/AnalysisUtils.cpp
    Source/analysis/HitAnalyzer.cpp
    Source/analysis/OverlapAnalyzer.cpp
    Source/analysis/AnalysisCore.cpp
    Source/analysis/Suggestions.cpp)

if(PHASECHECK_BUILD_AS_INSTRUMENT)
    set(PC_IS_SYNTH TRUE)
    set(PC_VST3_CATS Instrument Analyzer)
else()
    set(PC_IS_SYNTH FALSE)
    set(PC_VST3_CATS Fx Analyzer)
endif()

juce_add_plugin(PhaseCheck
    PRODUCT_NAME "Phase Check"
    COMPANY_NAME "PhaseCheck Audio"
    BUNDLE_ID com.phasecheckaudio.phasecheck
    PLUGIN_MANUFACTURER_CODE PhCk
    PLUGIN_CODE PhC1
    FORMATS VST3 Standalone
    IS_SYNTH ${PC_IS_SYNTH}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES ${PC_VST3_CATS}
    COPY_PLUGIN_AFTER_BUILD ${PHASECHECK_COPY_PLUGIN})

target_sources(PhaseCheck PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/gui/Theme.cpp
    Source/gui/Widgets.cpp
    Source/gui/HitModeView.cpp
    Source/gui/OverlapModeView.cpp
    Source/gui/FixPanel.cpp
    ${PHASECHECK_CORE_SOURCES})

target_include_directories(PhaseCheck PRIVATE Source)

target_compile_definitions(PhaseCheck PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PhaseCheck
    PRIVATE
        PhaseCheckFonts
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(PhaseCheck PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# Unit tests (ctest)
# ---------------------------------------------------------------------------
if(PHASECHECK_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(PhaseCheckTests PRODUCT_NAME "PhaseCheckTests")
    target_sources(PhaseCheckTests PRIVATE tests/TestMain.cpp ${PHASECHECK_CORE_SOURCES})
    target_include_directories(PhaseCheckTests PRIVATE Source tests)
    target_compile_definitions(PhaseCheckTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(PhaseCheckTests
        PRIVATE juce::juce_core juce::juce_audio_basics
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(MSVC)
        target_compile_options(PhaseCheckTests PRIVATE /utf-8)
    endif()
    add_test(NAME PhaseCheckTests COMMAND PhaseCheckTests)
endif()

# ---------------------------------------------------------------------------
# Developer tool: renders screenshots of the editor with synthetic audio
# ---------------------------------------------------------------------------
option(PHASECHECK_BUILD_SCREENSHOT_TOOL "Build the screenshot tool (developer use)" OFF)
if(PHASECHECK_BUILD_SCREENSHOT_TOOL)
    juce_add_console_app(PhaseCheckScreenshots PRODUCT_NAME "PhaseCheckScreenshots")
    target_sources(PhaseCheckScreenshots PRIVATE
        tools/RenderScreenshots.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/gui/Theme.cpp
        Source/gui/Widgets.cpp
        Source/gui/HitModeView.cpp
        Source/gui/OverlapModeView.cpp
        Source/gui/FixPanel.cpp
        ${PHASECHECK_CORE_SOURCES})
    target_include_directories(PhaseCheckScreenshots PRIVATE Source tests)
    target_compile_definitions(PhaseCheckScreenshots PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Phase Check" JucePlugin_IsSynth=0)
    target_link_libraries(PhaseCheckScreenshots PRIVATE
        PhaseCheckFonts juce::juce_audio_utils juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
