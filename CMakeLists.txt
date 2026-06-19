cmake_minimum_required(VERSION 3.21)

project(MondoEqRef VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)
FetchContent_MakeAvailable(juce)

juce_add_plugin(MondoEqRef
    COMPANY_NAME "Mondo"
    PLUGIN_MANUFACTURER_CODE "Mndo"
    PLUGIN_CODE "Meqr"
    FORMATS VST3
    PRODUCT_NAME "MondoEqRef"
)

juce_generate_juce_header(MondoEqRef)

target_sources(MondoEqRef
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(MondoEqRef
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MondoEqRef
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
