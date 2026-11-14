#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

// 4OSC settings that aren't automatable parameters (waveforms, unison, filter type,
// switches) plus its modulation matrix, presented like ordinary parameters.
namespace SynthSettings
{
    juce::Array<juce::var> describe (te::FourOscPlugin&, const juce::String& filter);

    // Returns false if key isn't one of these settings. Writes a human-readable
    // "id = value" into applied.
    bool apply (te::FourOscPlugin&, const juce::String& key, double value, juce::String& applied);
}
