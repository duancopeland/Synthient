#pragma once

#include "Song.h"

class StemsWindow;
class ToolBridge;

// Hosts the web UI and exposes the Song to it through native functions.
// Every edit from the UI or Claude goes through Song::runTool.
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

private:
    juce::WebBrowserComponent::Options createBrowserOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    void chooseFile (const juce::String& title, const juce::String& patterns, bool forSaving,
                     std::function<void (juce::File)> onChosen);
    void showPluginScanner();
    void showAudioSettings();
    void prepareDefaultScanPaths();
    void scanBundledPlugins();
    void openStemsWindow (const juce::File& source);
    juce::String addStemsAsTracks (const juce::var& stems);
    void emitToUi (const juce::var& event);   // received by app.js as a "claudedaw" event
    juce::OwnedArray<StemsWindow> stemsWindows;

    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<Song> song;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::unique_ptr<ToolBridge> bridge;   // Claude Code drives the app through this (Bridge.h)
    std::shared_ptr<juce::FileChooser> activeChooser;

    // Autosave every couple of minutes; a lock file left behind means the last session crashed.
    void timerCallback() override;
    juce::File getAutosaveFile() const;
    juce::File getLockFile() const;
    juce::int64 lastAutosaveHash = 0;
    bool recoveryAvailable = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
