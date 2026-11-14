#pragma once

#include <JuceHeader.h>

// Sends a Messages API request body to Anthropic on a background thread.
// The API key is read from ANTHROPIC_API_KEY here, on the native side, and is
// never exposed to the web UI.
namespace ClaudeClient
{
    // onDone is called on the message thread with the raw response body
    // (or a JSON error object if the request could not be made).
    void postMessages (const juce::String& requestBodyJson,
                       std::function<void (juce::String responseJson)> onDone);
}
