cmake_minimum_required(VERSION 3.22)

project(ClaudeDAW VERSION 0.1.0)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/external/webview2" CACHE PATH "")

add_subdirectory(external/tracktion_engine/modules/juce)
add_subdirectory(external/tracktion_engine/modules)

juce_add_gui_app(ClaudeDAW
    PRODUCT_NAME "Synthient"
    COMPANY_NAME "Synthient"
    BUNDLE_ID "com.claudedaw.app"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    NEEDS_WEBVIEW2 TRUE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER TRUE)

juce_generate_juce_header(ClaudeDAW)

target_compile_features(ClaudeDAW PRIVATE cxx_std_20)

target_sources(ClaudeDAW PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/ClaudeClient.cpp
    Source/Song.cpp
    Source/Sidecar.cpp
    Source/SongExtras.cpp
    Source/SongClips.cpp
    Source/SongIO.cpp
    Source/SongLocks.cpp
    Source/WinNotify.cpp
    Source/SynthSettings.cpp
    Source/Sampler/SfzInstrument.cpp
    Source/Sampler/ClaudeSamplerPlugin.cpp
    Source/StemsWindow.cpp
    Source/Diagnostics.cpp
    Source/Bridge.cpp)

target_compile_definitions(ClaudeDAW PRIVATE
    CLAUDEDAW_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ui"
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    JUCE_APPLICATION_NAME_STRING="Synthient"
    JUCE_APPLICATION_VERSION_STRING="0.1.0")

target_link_libraries(ClaudeDAW PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_warning_flags)

# Release builds keep debug symbols (Synthient.pdb) so crashes can be diagnosed.
if (MSVC)
    target_compile_options(ClaudeDAW PRIVATE "$<$<CONFIG:Release>:/Zi>")
    target_link_options(ClaudeDAW PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
endif()
