cmake_minimum_required(VERSION 3.22)

project(Reverbulator VERSION 0.1.0 LANGUAGES C CXX)

set(REVERBULATOR_JUCE_SOURCE_DIR "" CACHE PATH "Path to a JUCE checkout")

if(NOT REVERBULATOR_JUCE_SOURCE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
        set(REVERBULATOR_JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    elseif(WIN32 AND EXISTS "F:/JUCE/CMakeLists.txt")
        set(REVERBULATOR_JUCE_SOURCE_DIR "F:/JUCE")
    else()
        message(FATAL_ERROR
            "JUCE was not found. Pass -DREVERBULATOR_JUCE_SOURCE_DIR=/path/to/JUCE")
    endif()
endif()

add_subdirectory("${REVERBULATOR_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

set(REVERBULATOR_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND REVERBULATOR_FORMATS AU)
endif()

juce_add_plugin(Reverbulator
    VERSION 0.1.0
    COMPANY_NAME "MonkeMountainStudios"
    COMPANY_WEBSITE "https://monkemountainstudios.neocities.org"
    BUNDLE_ID "com.monkemountainstudios.reverbulator"
    ICON_BIG "Assets/ReverbulatorIcon.png"
    ICON_SMALL "Assets/ReverbulatorIcon.png"
    PLUGIN_MANUFACTURER_CODE Mnke
    PLUGIN_CODE Srs4
    FORMATS ${REVERBULATOR_FORMATS}
    PRODUCT_NAME "Reverbulator SRS-4"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(Reverbulator)

target_sources(Reverbulator PRIVATE
    Source/ChorusedSpaceLayer.cpp
    Source/ChorusedSpaceLayer.h
    Source/DigitalSpectralLayer.cpp
    Source/DigitalSpectralLayer.h
    Source/GravityBloomLayer.cpp
    Source/GravityBloomLayer.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/SerialRouting.h
    Source/StageToneFilter.h
    Source/TotalSpectralReturn.cpp
    Source/TotalSpectralReturn.h
    Source/WarmRoomLayer.cpp
    Source/WarmRoomLayer.h
)

target_compile_features(Reverbulator PRIVATE cxx_std_17)

target_compile_definitions(Reverbulator PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

if(WIN32)
    target_compile_definitions(Reverbulator PUBLIC JUCE_ASIO=1)
endif()

target_link_libraries(Reverbulator PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)

target_sources(Reverbulator_Standalone PRIVATE Source/StandaloneApp.cpp)
target_compile_definitions(Reverbulator_Standalone PRIVATE
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

juce_add_console_app(ReverbulatorRender PRODUCT_NAME "Reverbulator UI Renderer")
juce_generate_juce_header(ReverbulatorRender)
target_sources(ReverbulatorRender PRIVATE Source/RenderUI.cpp)
target_include_directories(ReverbulatorRender PRIVATE Source)
target_compile_features(ReverbulatorRender PRIVATE cxx_std_17)
target_link_libraries(ReverbulatorRender PRIVATE
    Reverbulator
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
)

juce_add_console_app(ReverbulatorDspTests PRODUCT_NAME "Reverbulator DSP Tests")
juce_generate_juce_header(ReverbulatorDspTests)
target_sources(ReverbulatorDspTests PRIVATE
    Tests/WarmRoomLayerTest.cpp
    Source/ChorusedSpaceLayer.cpp
    Source/ChorusedSpaceLayer.h
    Source/DigitalSpectralLayer.cpp
    Source/DigitalSpectralLayer.h
    Source/GravityBloomLayer.cpp
    Source/GravityBloomLayer.h
    Source/SerialRouting.h
    Source/StageToneFilter.h
    Source/TotalSpectralReturn.cpp
    Source/TotalSpectralReturn.h
    Source/WarmRoomLayer.cpp
    Source/WarmRoomLayer.h
)
target_include_directories(ReverbulatorDspTests PRIVATE Source)
target_compile_features(ReverbulatorDspTests PRIVATE cxx_std_17)
target_compile_definitions(ReverbulatorDspTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(ReverbulatorDspTests PRIVATE
    juce::juce_audio_basics
    juce::juce_core
    juce::juce_dsp
)

juce_add_console_app(ReverbulatorAudioProbe PRODUCT_NAME "Reverbulator Audio Probe")
juce_generate_juce_header(ReverbulatorAudioProbe)
target_sources(ReverbulatorAudioProbe PRIVATE Tests/AudioDeviceProbe.cpp)
target_compile_features(ReverbulatorAudioProbe PRIVATE cxx_std_17)
target_compile_definitions(ReverbulatorAudioProbe PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
if(WIN32)
    target_compile_definitions(ReverbulatorAudioProbe PRIVATE JUCE_ASIO=1)
endif()
target_link_libraries(ReverbulatorAudioProbe PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_utils
)

