#pragma once

#include "SfzInstrument.h"

namespace te = tracktion;

// Synthient's multi-sample instrument: plays an .sfz file (velocity layers,
// round-robins, keyswitches, loops, release samples, chords). The .sfz path
// lives in the plugin's ValueTree, so songs save/load it and undo works.
class ClaudeSamplerPlugin  : public te::Plugin
{
public:
    ClaudeSamplerPlugin (te::PluginCreationInfo);
    ~ClaudeSamplerPlugin() override;

    static const char* getPluginName()                  { return "Instrument"; }
    static const char* xmlTypeName;

    // Library instruments are stored relative to this folder so songs survive the library moving.
    static inline std::function<juce::File()> libraryRoot;

    // Loads an .sfz (absolute path) as one undoable change. Returns an error, or empty on success.
    juce::String setInstrument (const juce::File& sfzFile, const juce::String& displayName);

    juce::String getInstrumentName() const              { return displayName.get(); }
    juce::String getSfzPath() const                     { return sfzPath.get(); }
    juce::String getLoadError() const                   { return loadError; }
    int getNumRegions() const;
    juce::Range<int> getKeyRange() const;
    juce::Array<int> getKeyswitches() const;

    juce::String getName() const override               { return displayName.get().isNotEmpty() ? displayName.get() : juce::String ("Instrument"); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Inst"; }
    juce::String getSelectableDescription() override    { return "Instrument"; }
    bool isSynth() override                             { return true; }
    bool takesMidiInput() override                      { return true; }
    bool takesAudioInput() override                     { return false; }
    bool producesAudioWhenNoAudioInput() override       { return true; }
    int getNumOutputChannelsGivenInputs (int) override  { return 2; }
    BusLayout getBusses() const override                { return BusLayout::singleStereoInOut(); }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void midiPanic() override;
    void applyToBuffer (const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    te::AutomatableParameter::Ptr volume, pan, tune, attack, release, velocitySensitivity;

private:
    struct Loaded
    {
        std::shared_ptr<const sfz::Instrument> instrument;
        std::vector<int> seqCounters;
    };

    struct Voice
    {
        bool active = false;
        const sfz::Region* region = nullptr;
        const sfz::Sample* sample = nullptr;
        int note = 0, velocity = 0;
        juce::uint64 age = 0;
        double pos = 0, ratio = 1;      // ratio excludes pitch bend and the tune parameter
        float gain = 1, gainL = 1, gainR = 1;
        juce::int64 delay = 0;
        bool released = false, sustainedByPedal = false, releaseVoice = false, looping = false;

        enum class Stage { delay, attack, hold, decay, sustain, release };
        Stage stage = Stage::delay;
        float level = 0, sustainLevel = 1, attackStep = 1, decayStep = 1, releaseCoef = 0.99f, releaseSeconds = 0.01f;
        juce::int64 stageSamples = 0;   // samples left in delay/hold
        juce::int64 holdSamples = 0;
        juce::uint64 eventId = 0;

        bool filtered = false;
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] {}, z2[2] {};
    };

    static constexpr int maxVoices = 96;

    juce::CachedValue<juce::String> sfzPath, displayName;
    juce::CachedValue<float> volumeValue, panValue, tuneValue, attackValue, releaseValue, velSensValue;

    // message thread
    std::vector<std::shared_ptr<Loaded>> keepAlive; // never freed on the audio thread
    juce::String loadError;
    std::shared_ptr<const sfz::Instrument> preloaded; // set by setInstrument so the reload doesn't parse twice
    void reloadFromState();

    // handed to the audio thread
    juce::SpinLock pendingLock;
    std::shared_ptr<Loaded> pending;
    bool hasPending = false;

    // audio thread
    std::shared_ptr<Loaded> active;
    std::array<Voice, maxVoices> voices;
    std::array<float, 256> cc {};
    std::array<bool, 128> keyDown {}, pendingRelease {};
    std::array<int, 128> keyVelocity {};
    std::array<juce::int64, 128> keyDownAt {};
    int keysHeld = 0, currentKeyswitch = -1, blockPos = 0;
    juce::uint64 eventCounter = 0;
    bool pedal = false, alternate = false;
    float pitchBend = 0;               // -1..1
    juce::int64 sampleClock = 0;
    juce::uint64 voiceCounter = 0;
    juce::Random random;
    bool killAll = false;

    void takePending();
    void noteOn (int note, int velocity, int offset);
    void noteOff (int note, int offset);
    void releaseKey (int note, int offset);
    bool regionMatches (const sfz::Region&, int index, int note, int velocity, float rand);
    void startVoice (const sfz::Region&, int note, int velocity, int offset, float extraDb, juce::uint64 eventId);
    void releaseVoice (Voice&, bool fast);
    void renderVoices (juce::AudioBuffer<float>&, int start, int num);
    void renderVoice (Voice&, float* left, float* right, int num, double pitchFactor);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClaudeSamplerPlugin)
};
