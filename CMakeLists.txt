cmake_minimum_required(VERSION 3.22)
project(meowximizer VERSION 0.2.0 LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.16-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)
FetchContent_Declare(cat_ui GIT_REPOSITORY https://github.com/SpeakingColors/cat-ui GIT_TAG ba762e1755bbabd6847076e26c5ed7a9c5d6c76d)
FetchContent_MakeAvailable(cat_ui)

juce_add_plugin(Meowximizer
    COMPANY_NAME "Studio 87"
    PLUGIN_MANUFACTURER_CODE Spco
    PLUGIN_CODE Mmax
    FORMATS VST3 Standalone
    PRODUCT_NAME "Meowximizer"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE)
target_sources(Meowximizer PRIVATE src/common/FxBase.cpp src/common/FxEditor.cpp src/Plugins.cpp src/maximizer/Meowximizer.cpp)
target_include_directories(Meowximizer PRIVATE src)
target_compile_definitions(Meowximizer PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Meowximizer PRIVATE cat_ui juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
