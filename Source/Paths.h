#pragma once

#include <JuceHeader.h>

// Finds an app folder (ui, analysis, instruments). An installed copy keeps them
// next to Synthient.exe; a development build falls back to the source tree.
// Where settings, logs, autosave and Claude memory live: %APPDATA%\Synthient, or the folder in the
// SYNTHIENT_DATA_DIR environment variable (test copies use that so they never touch the user's data).
inline juce::File getUserDataDir()
{
    auto overridden = juce::SystemStats::getEnvironmentVariable ("SYNTHIENT_DATA_DIR", {});
    if (overridden.isNotEmpty())
        return juce::File (overridden);

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Synthient");
}

inline juce::File getAppFolder (const juce::String& name)
{
    auto installed = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                         .getParentDirectory().getChildFile (name);

    if (installed.isDirectory())
        return installed;

    return juce::File (CLAUDEDAW_UI_DIR).getSiblingFile (name);
}
