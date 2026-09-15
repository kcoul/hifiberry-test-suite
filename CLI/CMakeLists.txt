project(HiFiBerryTestSuiteCLI VERSION 0.1)

set (TargetName ${PROJECT_NAME})

juce_add_console_app(${TargetName} PRODUCT_NAME "HiFiBerry Test Suite CLI")

target_sources(${TargetName} PRIVATE
        Main.cpp
        SelfTest.cpp)

target_compile_definitions(${TargetName} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:${TargetName},JUCE_VERSION>")

# No GUI modules, so the CLI runs on headless Linux and QNX targets without X11 or Screen.
target_link_libraries(${TargetName} PRIVATE
        HiFiBerryTestSuiteCommon
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
        juce::juce_audio_devices
        juce::juce_audio_formats)
