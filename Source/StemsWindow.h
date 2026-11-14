#pragma once

#include <JuceHeader.h>
#include "Sidecar.h"

// A separate window that splits an audio file into drums/bass/vocals/other with
// Demucs while the rest of Synthient stays usable.
class StemsWindow  : public juce::DocumentWindow
{
public:
    struct Callbacks
    {
        std::function<juce::String (const juce::var& stems)> addAsTracks; // returns an error or empty
        std::function<void (const juce::var& stems)> finished;            // success only
        std::function<void (StemsWindow&)> closed;                        // owner deletes the window
    };

    StemsWindow (const juce::File& source, const juce::File& outDir, Callbacks);
    ~StemsWindow() override;

    void closeButtonPressed() override;

private:
    class Content;
    Content* content = nullptr;
    Callbacks callbacks;
    std::shared_ptr<Sidecar::Job> job = std::make_shared<Sidecar::Job>();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemsWindow)
};
