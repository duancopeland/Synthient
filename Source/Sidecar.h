#pragma once

#include <JuceHeader.h>

// Runs the Python analysis tools in analysis/ (see analysis/README.md) as a
// child process on a background thread. Each tool prints one JSON object.
namespace Sidecar
{
    juce::File getAnalysisDir();

    // onDone is called on the message thread with the parsed JSON, or an
    // object { "error": ... } if the tool could not be run.
    void run (const juce::StringArray& toolAndArgs, std::function<void (juce::var)> onDone);

    // A long-running tool. cancel() can be called from any thread.
    struct Job
    {
        std::atomic<bool> cancelled { false };
        juce::ChildProcess process;
        void cancel() { cancelled = true; process.kill(); }
    };

    // Every JSON line the tool prints goes to onLine (message thread); the last one also to onDone.
    void runStreaming (const juce::StringArray& toolAndArgs, std::shared_ptr<Job>,
                       std::function<void (juce::var)> onLine, std::function<void (juce::var)> onDone);
}
