cmake_minimum_required(VERSION 3.22)
project(ts_harness VERSION 0.1.0)
set(JUCE_SRC "" CACHE PATH "JUCE tree")
add_subdirectory(${JUCE_SRC} juce EXCLUDE_FROM_ALL)
juce_add_console_app(ts-harness PRODUCT_NAME ts-harness)
target_sources(ts-harness PRIVATE main.cpp)
target_compile_definitions(ts-harness PRIVATE JUCE_JACK=1 JUCE_ALSA=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
    JUCE_MODULE_AVAILABLE_juce_audio_devices=1)
target_link_libraries(ts-harness PRIVATE juce::juce_audio_devices juce::juce_audio_basics juce::juce_events
    juce::juce_core juce::juce_recommended_config_flags)
