cmake_minimum_required (VERSION 3.22)
project (TimestampsApple VERSION 0.0.1 LANGUAGES C CXX OBJC OBJCXX)

# The repository root is the JUCE checkout under test
add_subdirectory ("${CMAKE_CURRENT_SOURCE_DIR}/../../.." juce EXCLUDE_FROM_ALL)

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Compiles juce_audio_devices (juce_Audio_ios.cpp) for the iOS simulator. A static library is
    # enough: nothing is linked or run.
    add_library (ios_compile_probe STATIC IosProbe.cpp)
    target_link_libraries (ios_compile_probe PRIVATE juce::juce_audio_devices juce::juce_recommended_config_flags)
else()
    # Opens two virtual HAL devices as one combined CoreAudio device and checks the timestamps
    juce_add_console_app (CombinedDeviceCheck PRODUCT_NAME "CombinedDeviceCheck")
    target_sources (CombinedDeviceCheck PRIVATE Main.cpp)
    target_compile_definitions (CombinedDeviceCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries (CombinedDeviceCheck PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
