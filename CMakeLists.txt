cmake_minimum_required(VERSION 3.21)

project(MondoEqRef VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)
set(JUCE_GLOBAL_ASIO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ASIOSDK" CACHE PATH "Path to ASIO SDK")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ASIOSDK" "${CMAKE_CURRENT_SOURCE_DIR}/ASIOSDK/common")
FetchContent_MakeAvailable(juce)

juce_add_binary_data(MondoEqRefAssets
    HEADER_NAME "Assets.h"
    NAMESPACE Assets
    SOURCES icon.png
)

juce_add_plugin(MondoEqRef
    COMPANY_NAME "Mondo"
    PLUGIN_MANUFACTURER_CODE "Mndo"
    PLUGIN_CODE "Meqr"
    FORMATS VST3
    PRODUCT_NAME "MondoEqRef"
    ICON_BIG icon.png
    ICON_SMALL icon.png
)

juce_generate_juce_header(MondoEqRef)

set(SHARED_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_sources(MondoEqRef PRIVATE ${SHARED_SOURCES})

target_compile_definitions(MondoEqRef
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_ASIO=1
        JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(MondoEqRef
    PRIVATE
        MondoEqRefAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- MondoSpectralRef Plugin ---
juce_add_plugin(MondoSpectralRef
    COMPANY_NAME "Mondo"
    PLUGIN_MANUFACTURER_CODE "Mndo"
    PLUGIN_CODE "Mspr"
    FORMATS VST3
    PRODUCT_NAME "MondoSpectralRef"
    ICON_BIG icon.png
    ICON_SMALL icon.png
)

juce_generate_juce_header(MondoSpectralRef)

set(SPECTRAL_SHARED_SOURCES
    Source/SpectralRef/MondoSpectralRefProcessor.cpp
    Source/SpectralRef/MondoSpectralRefEditor.cpp
)

target_sources(MondoSpectralRef PRIVATE ${SPECTRAL_SHARED_SOURCES})

target_compile_definitions(MondoSpectralRef
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_ASIO=1
        JUCE_USE_MP3AUDIOFORMAT=1
        MONDO_SPECTRAL_REF_PLUGIN=1
)

target_link_libraries(MondoSpectralRef
    PRIVATE
        MondoEqRefAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Standalone App Target ---
juce_add_gui_app(MondoEqRefApp
    PRODUCT_NAME "MondoEqRef Standalone"
    ICON_BIG icon.png
    ICON_SMALL icon.png
)

target_sources(MondoEqRefApp
    PRIVATE
        ${SHARED_SOURCES}
        ${SPECTRAL_SHARED_SOURCES}
        Source/App/Main.cpp
        Source/App/MainComponent.cpp
        Source/App/AudioNormalizer.cpp
        Source/App/DeviceRouting.cpp
        Source/App/Views/AnalyzeView.cpp
        Source/App/Views/GuitarDIView.cpp
        Source/App/Views/StemsView.cpp
        Source/App/Views/CustomAudioSettings.cpp
        Source/App/Views/SpectralRefView.cpp
)

target_compile_definitions(MondoEqRefApp
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="MondoEqRef"
        JUCE_ASIO=1
        JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(MondoEqRefApp
    PRIVATE
        MondoEqRefAssets
        juce::juce_audio_utils
        juce::juce_dsp
        setupapi
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
