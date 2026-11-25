cmake_minimum_required(VERSION 3.22)

project(AbleAEC VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

option(ABLEAEC_BUILD_PLUGIN "Build the JUCE plugin (VST3 / AU)" ON)
option(ABLEAEC_BUILD_TESTS "Build the DSP simulation test" ON)
set(JUCE_DIR "" CACHE PATH "Path to an existing JUCE checkout (empty = download JUCE 8.0.4)")

# ------------------------------------------------------------------ DSP core (no dependencies)
add_library(ableaec_dsp STATIC src/dsp/AecCore.cpp)
target_include_directories(ableaec_dsp PUBLIC src/dsp)
set_target_properties(ableaec_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# offline WAV processor (also handy for checking your own recordings)
add_executable(aec_wav tools/aec_wav.cpp)
target_link_libraries(aec_wav PRIVATE ableaec_dsp)

if(ABLEAEC_BUILD_TESTS)
    enable_testing()
    add_executable(aec_sim_test tests/AecSimTest.cpp)
    target_link_libraries(aec_sim_test PRIVATE ableaec_dsp)
    add_test(NAME aec_sim_test COMMAND aec_sim_test)
endif()

# ------------------------------------------------------------------ plugin
if(ABLEAEC_BUILD_PLUGIN)
    if(JUCE_DIR)
        add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(ABLEAEC_FORMATS VST3)
    if(APPLE)
        list(APPEND ABLEAEC_FORMATS AU)
    endif()

    juce_add_plugin(AbleAEC
        COMPANY_NAME "ableAEC"
        BUNDLE_ID "com.ableaec.ableaec"
        PLUGIN_MANUFACTURER_CODE Ablc
        PLUGIN_CODE Aaec
        FORMATS ${ABLEAEC_FORMATS}
        PRODUCT_NAME "AbleAEC"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Restoration
        AU_MAIN_TYPE kAudioUnitType_Effect)

    target_sources(AbleAEC PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp)

    # UI font: Nanum Gothic subset (SIL OFL 1.1, see resources/fonts/OFL.txt), rebuilt by tools/make_ui_font.py
    juce_add_binary_data(AbleAEC_Fonts SOURCES
        resources/fonts/AbleAECUiKR-Regular.ttf
        resources/fonts/AbleAECUiKR-Bold.ttf)
    set_target_properties(AbleAEC_Fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(MSVC)
        target_compile_options(AbleAEC PRIVATE /utf-8) # UiStrings.h holds UTF-8 Korean text
    endif()

    target_compile_definitions(AbleAEC PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(AbleAEC
        PRIVATE
            ableaec_dsp
            AbleAEC_Fonts
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # End-to-end test that hosts the built VST3 (like a DAW) with the sidechain bus enabled.
    if(ABLEAEC_BUILD_TESTS)
        juce_add_console_app(AbleAecHostTest PRODUCT_NAME "AbleAecHostTest")
        target_sources(AbleAecHostTest PRIVATE tests/PluginHostTest.cpp)
        target_compile_definitions(AbleAecHostTest PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
        target_link_libraries(AbleAecHostTest PRIVATE
            juce::juce_audio_processors
            juce::juce_recommended_config_flags)
        add_dependencies(AbleAecHostTest AbleAEC_VST3)
        add_test(NAME plugin_host_test
                 COMMAND AbleAecHostTest "$<GENEX_EVAL:$<TARGET_PROPERTY:AbleAEC_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    endif()
endif()
