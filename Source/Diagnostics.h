#pragma once

#include <JuceHeader.h>

// A small activity log plus crash and freeze reports, so a problem the user hit can be
// diagnosed afterwards. Everything goes to %APPDATA%\Synthient\logs:
//   Synthient.log          - what the app was doing (tools, Claude requests, renders, errors)
//   crash-<time>.dmp/.txt  - written when the app crashes
//   freeze-<time>.dmp      - written once when the window stops responding for 10 s
namespace Diagnostics
{
    void start();                          // call once, on the message thread, at startup
    void stop();
    void log (const juce::String& line);   // thread-safe
    juce::File getLogFolder();

    // If the previous session didn't close normally: { crashed, kind ("crash"|"freeze"|"closed"),
    // time, log (that session's log text) }. Otherwise { crashed: false }.
    juce::var getPreviousSessionProblem();

    // Zips the previous session's log and any crash/freeze reports for emailing. Returns the zip.
    juce::File writeCrashReport();
}
