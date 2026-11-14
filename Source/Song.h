#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

// Wraps the Tracktion Edit, which is the single source of truth for the song.
// All edits (from the UI or from Claude) go through runTool() so they share
// validation and undo.
//
// Model: any number of tracks, each with one instrument, an effects chain,
// mixer settings and one MIDI clip spanning the whole song (song length is
// in bars, stored on the Edit).
class Song
{
public:
    explicit Song (te::Engine&);
    ~Song();

    te::Edit& getEdit()                 { return *edit; }
    juce::File getFile() const          { return file; }

    void newSong();
    juce::String load (const juce::File&);          // returns error or empty
    juce::String save (const juce::File&);
    juce::String saveCopy (const juce::File&);      // writes without changing the song's file
    void forgetFile()                   { file = juce::File(); }
    // Renders the song on a background thread; onDone(error) runs on the message thread.
    void exportWav (const juce::File&, std::function<void (juce::String error)> onDone);

    // SongIO.cpp. Format follows the file extension: wav, ogg, flac or mp3.
    void exportAudio (const juce::File&, std::function<void (juce::String error)> onDone);
    void exportStems (const juce::File& folder, const juce::String& extension,
                      std::function<void (juce::String error, juce::StringArray files)> onDone);
    juce::String exportMidi (const juce::File&, int trackIndex);   // -1 = all tracks
    bool isRendering() const            { return renderHandle != nullptr; }

    juce::var getState (bool includeNotes = true);
    juce::var getTransport();       // position and play state only (cheap, for the playhead)
    juce::var runTool (const juce::String& name, const juce::var& input);

    te::Plugin* findDevice (int trackIndex, const juce::var& device);

    static constexpr int beatsPerBar = 4;

    struct NoteData { int pitch; double start, length; int velocity; };
    struct ClipBeats { te::Clip::Ptr clip; double start, end, offset; };

private:
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    juce::File file;
    std::shared_ptr<te::EditRenderer::Handle> renderHandle;
    int renderGeneration = 0;   // ignores late callbacks from cancelled renders
    std::shared_ptr<bool> alive = std::make_shared<bool> (true); // guards async render callbacks

    void initialiseEdit();
    int getBars();
    double getSongBeats()               { return getBars() * (double) beatsPerBar; }
    void updateLoopRange();

    juce::Array<te::AudioTrack*> getTracks();
    te::AudioTrack* getTrack (const juce::var& input);
    // SongClips.cpp
    juce::Array<te::MidiClip*> getMidiClips (te::AudioTrack&);
    te::MidiClip* createMidiClip (te::AudioTrack&, double fromBeat, double toBeat, const juce::String& name);
    std::vector<NoteData> readTrackNotes (te::AudioTrack&);
    void writeTrackNotes (te::AudioTrack&, const juce::Array<juce::var>& notes, bool clearFirst);
    juce::var describeMidiClips (te::AudioTrack&);
    std::vector<ClipBeats> captureClipBeats();
    void restoreClipBeats (const std::vector<ClipBeats>&);
    struct AutomationBeats { te::AutomatableParameter::Ptr param; std::vector<std::tuple<double, float, float>> points; };
    std::vector<AutomationBeats> captureAutomationBeats();
    void restoreAutomationBeats (const std::vector<AutomationBeats>&);
    void renderTo (const juce::File&, juce::AudioFormat*, const juce::BigInteger& tracks, bool useMasterPlugins,
                   std::function<void (juce::String)> onDone);
    void renderFile (const juce::File&, const juce::BigInteger& tracks, bool useMasterPlugins,
                     std::function<void (juce::String)> onDone);   // format by extension, incl. mp3
    juce::var toolImportMidi (const juce::var&);

    // SongLocks.cpp
    juce::Array<juce::var> readLocks();
    void writeLocks (const juce::Array<juce::var>&);
    std::vector<std::pair<double, double>> getLockedRanges (te::AudioTrack&);
    bool isTrackLocked (te::AudioTrack&);
    bool isBeatLocked (te::AudioTrack&, double beat);
    bool rangeTouchesLock (te::AudioTrack&, double from, double to);
    juce::var describeLocks (te::AudioTrack&);
    juce::String checkLocks (const juce::String& tool, const juce::var& input);
    juce::var toolLock (const juce::var&);
    juce::var toolUnlock (const juce::var&);
    int lockedNotesKept = 0;    // notes a tool tried to change inside locked bars
    juce::String lastCoalesceKey;
    juce::var toolAddClip (const juce::var&);
    juce::var toolMoveClip (const juce::var&);
    juce::var toolResizeClip (const juce::var&);
    juce::var toolDeleteClip (const juce::var&);
    te::Plugin* getInstrument (te::AudioTrack&);
    juce::Array<te::Plugin*> getEffects (te::AudioTrack&);
    te::AudioTrack* createTrack (const juce::String& name, te::Plugin::Ptr instrument);

    te::Plugin::Ptr createInstrument (const juce::String& name, juce::String& error);
    // Gives a track a Surge XT patch by name (falls back to 4OSC when Surge XT isn't installed).
    void useDefaultSynth (te::AudioTrack&, const juce::String& patchName);
    static juce::File findSynthPatch (const juce::String& name);
    te::Plugin::Ptr createEffect (const juce::String& name, juce::String& error);
    std::optional<juce::PluginDescription> findExternalPlugin (const juce::String& name, bool instrument);

    juce::var describeDevice (te::Plugin&, int index);
    juce::var describeParams (te::Plugin&, const juce::String& filter, int limit);
    juce::var applyParams (te::Plugin&, const juce::var& params);

    // Tools
    juce::var toolAddTrack (const juce::var&);
    juce::var toolRemoveTrack (const juce::var&);
    juce::var toolSetTrack (const juce::var&);
    juce::var toolAddNotes (const juce::var&);
    juce::var toolRemoveNotes (const juce::var&);
    juce::var toolSetTempo (const juce::var&);
    juce::var toolSetSongLength (const juce::var&);
    juce::var toolListInstruments (const juce::var&);
    juce::var toolSetInstrument (const juce::var&);
    juce::var toolListEffects (const juce::var&);
    juce::var toolAddEffect (const juce::var&);
    juce::var toolRemoveEffect (const juce::var&);
    juce::var toolGetDeviceParams (const juce::var&);
    juce::var toolListPrograms (const juce::var&);
    juce::var toolSetProgram (const juce::var&);
    juce::var toolSetDeviceParams (const juce::var&);
    juce::var toolLoadSample (const juce::var&);
    juce::var toolLoadSfz (const juce::var&);
    juce::var toolLoadSynthPatch (const juce::var&);
    juce::var toolSetAutomation (const juce::var&);
    juce::var toolClearAutomation (const juce::var&);
    juce::var toolFade (const juce::var&);

    // Automation targets: device "mixer" (params "volume" in dB, "pan" -1..1),
    // "instrument", or an effect index. track -1 means the master bus.
    struct AutomationTarget { te::AutomatableParameter* param = nullptr; bool isVolumeDb = false; juce::String error; };
    AutomationTarget findAutomationTarget (const juce::var& input);
    // SongExtras.cpp
    juce::Array<int> trackListFrom (const juce::var& input);
    te::PluginList* getPluginListFor (int trackIndex);
    juce::Array<te::Plugin*> getMasterEffects();
    te::AuxReturnPlugin* getAuxReturn (te::AudioTrack&);
    juce::var describeSends (te::AudioTrack&);
    juce::var describeAudioClips (te::AudioTrack&);
    juce::var describeTakes (te::AudioTrack&);
    juce::var describeSamplerSounds (te::AudioTrack&);
    juce::var getSections();
    juce::var getKeyInfo();
    juce::var readTakes (te::AudioTrack&);
    void writeTakes (te::AudioTrack&, const juce::var&);

    juce::var toolAddBus (const juce::var&);
    juce::var toolSetSend (const juce::var&);
    juce::var toolSetSidechain (const juce::var&);
    juce::var toolSidechainDuck (const juce::var&);
    juce::var toolAddAudioClip (const juce::var&);
    juce::var toolRemoveAudioClip (const juce::var&);
    juce::var toolSetSections (const juce::var&);
    juce::var toolCopyBars (const juce::var&);
    juce::var toolClearBars (const juce::var&);
    juce::var toolGroove (const juce::var&);
    juce::var toolTranspose (const juce::var&);
    juce::var toolSetKey (const juce::var&);
    juce::var toolSaveTake (const juce::var&);
    juce::var toolUseTake (const juce::var&);
    juce::var toolDeleteTake (const juce::var&);

    juce::var describeAutomation (const juce::Array<te::AutomatableParameter*>&);
    te::TimePosition beatToTime (double beat);
};
