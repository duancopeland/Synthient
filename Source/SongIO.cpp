// File input/output for Song: audio export (WAV/OGG/FLAC/MP3), stems, MIDI import/export.
#include "Song.h"
#include "Sidecar.h"

using namespace std::literals;

namespace
{
    juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
    {
        auto obj = new juce::DynamicObject();
        for (auto& [k, v] : props)
            obj->setProperty (k, v);
        return juce::var (obj);
    }

    juce::var ok (std::initializer_list<std::pair<const char*, juce::var>> props = {})
    {
        auto v = makeObject (props);
        v.getDynamicObject()->setProperty ("ok", true);
        return v;
    }

    juce::var fail (const juce::String& message)
    {
        return makeObject ({ { "ok", false }, { "error", message } });
    }

    constexpr int midiTicksPerQuarter = 960;
}

//==============================================================================
void Song::renderTo (const juce::File& f, juce::AudioFormat* format, const juce::BigInteger& tracks,
                     bool useMasterPlugins, std::function<void (juce::String)> onDone)
{
    if (renderHandle != nullptr)
        return onDone ("A render is already in progress");

    // The async renderer appends to an existing file, so start from an empty one.
    if (f.exists() && ! f.deleteFile())
        return onDone ("Could not overwrite " + f.getFullPathName() + " (is it open in another program?)");

    edit->getTransport().stop (false, true);

    te::Renderer::Parameters r (*edit);
    r.destFile = f;
    r.audioFormat = format;
    r.bitDepth = format == engine.getAudioFileFormatManager().getWavFormat() ? 24 : 16;
    r.sampleRateForAudio = engine.getDeviceManager().getSampleRate();
    r.blockSizeForAudio = engine.getDeviceManager().getBlockSize();
    r.time = { 0s, edit->tempoSequence.toTime ({ getBars(), {} }) };
    r.usePlugins = true;
    r.useMasterPlugins = useMasterPlugins;
    r.tracksToDo = tracks;

    auto qualities = format->getQualityOptions();
    if (qualities.contains ("320 kbps"))
        r.quality = qualities.indexOf ("320 kbps");
    else if (! qualities.isEmpty())
        r.quality = qualities.size() - 1;

    const int generation = ++renderGeneration;

    renderHandle = te::EditRenderer::render (r, [this, onDone, alive = alive, generation] (tl::expected<juce::File, std::string> result)
    {
        juce::String error = result.has_value() ? juce::String() : juce::String (result.error());

        juce::MessageManager::callAsync ([this, onDone, error, alive, generation]
        {
            // A render cancelled by New/Open may report late; it must not touch a newer render.
            if (! *alive || generation != renderGeneration)
                return;

            renderHandle = nullptr;
            onDone (error);
        });
    });
}

void Song::exportAudio (const juce::File& f, std::function<void (juce::String)> onDone)
{
    renderFile (f, te::toBitSet (te::getAllTracks (*edit)), true, std::move (onDone));
}

void Song::renderFile (const juce::File& f, const juce::BigInteger& tracks, bool useMasterPlugins,
                       std::function<void (juce::String)> onDone)
{
    auto& formats = engine.getAudioFileFormatManager();

    if (f.hasFileExtension ("mp3"))
    {
        // Render a WAV, then encode it with LAME via the analysis tools.
        auto wav = f.getSiblingFile (f.getFileNameWithoutExtension() + ".render-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)) + ".wav");
        if (f.exists() && ! f.deleteFile())
            return onDone ("Could not overwrite " + f.getFullPathName());
        renderTo (wav, formats.getWavFormat(), tracks, useMasterPlugins, [f, wav, onDone] (juce::String error)
        {
            if (error.isNotEmpty())
            {
                wav.deleteFile();
                return onDone (error);
            }

            Sidecar::run ({ "encode_mp3", wav.getFullPathName(), f.getFullPathName(), "--bitrate", "320" },
                          [f, wav, onDone] (juce::var json)
            {
                wav.deleteFile();
                onDone (f.existsAsFile() && ! json.hasProperty ("error") ? juce::String()
                                                                      : json.getProperty ("error", "MP3 encoding failed").toString());
            });
        });
        return;
    }

    auto format = formats.getFormatFromFileName (f);
    if (format == nullptr || ! format->canDoStereo())
        format = formats.getWavFormat();

    renderTo (f, format, tracks, useMasterPlugins, std::move (onDone));
}

void Song::exportWav (const juce::File& f, std::function<void (juce::String)> onDone)
{
    exportAudio (f, std::move (onDone));
}

void Song::exportStems (const juce::File& folder, const juce::String& extension,
                        std::function<void (juce::String error, juce::StringArray files)> onDone)
{
    folder.createDirectory();

    struct Job { juce::BigInteger bits; juce::File file; };
    auto jobs = std::make_shared<std::vector<Job>>();
    auto allTracks = te::getAllTracks (*edit);
    int n = 1;

    for (auto t : getTracks())
    {
        juce::BigInteger bits;
        bits.setBit (allTracks.indexOf (t));
        auto name = juce::String (n++).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (t->getName());
        jobs->push_back ({ bits, folder.getChildFile (name).withFileExtension (extension) });
    }

    auto done = std::make_shared<juce::StringArray>();
    auto step = std::make_shared<std::function<void (size_t)>>();

    *step = [this, jobs, done, step, onDone, alive = alive] (size_t i)
    {
        if (! *alive)
            return;

        if (i >= jobs->size())
        {
            onDone ({}, *done);
            *step = nullptr; // break the self-reference
            return;
        }

        auto& job = (*jobs)[i];

        renderFile (job.file, job.bits, false, [i, done, step, job, onDone] (juce::String error)
        {
            if (error.isNotEmpty())
            {
                onDone (job.file.getFileName() + ": " + error, *done);
                *step = nullptr;
                return;
            }

            done->add (job.file.getFullPathName());
            (*step) (i + 1);
        });
    };

    (*step) (0);
}

//==============================================================================
juce::String Song::exportMidi (const juce::File& f, int trackIndex)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (midiTicksPerQuarter);

    // Tempo track.
    {
        juce::MidiMessageSequence tempo;
        tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / edit->tempoSequence.getTempo (0)->getBpm())));
        tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
        file.addTrack (tempo);
    }

    int index = 0;
    for (auto t : getTracks())
    {
        if (trackIndex >= 0 && index++ != trackIndex)
            continue;

        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, t->getName()));

        for (auto& n : readTrackNotes (*t))
        {
            const double on = n.start * midiTicksPerQuarter, off = (n.start + n.length) * midiTicksPerQuarter;
            seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity), on);
            seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), off);
        }

        seq.updateMatchedPairs();
        file.addTrack (seq);
    }

    f.deleteFile();
    juce::FileOutputStream out (f);
    if (! out.openedOk() || ! file.writeTo (out))
        return "Could not write " + f.getFullPathName();

    return {};
}

juce::var Song::toolImportMidi (const juce::var& input)
{
    const juce::File f (input["path"].toString());
    juce::FileInputStream in (f);
    juce::MidiFile file;

    if (! in.openedOk() || ! file.readFrom (in))
        return fail ("Not a readable MIDI file: " + f.getFullPathName());

    // Ticks-per-quarter files map straight to beats; SMPTE files are converted via seconds.
    const bool smpte = file.getTimeFormat() <= 0;
    double ticksPerQuarter = smpte ? 1.0 : file.getTimeFormat();
    if (smpte)
    {
        file.convertTimestampTicksToSeconds();
        ticksPerQuarter = 60.0 / edit->tempoSequence.getTempo (0)->getBpm();   // seconds per beat
    }
    const double offset = input.getProperty ("start_beat", 0.0);
    if (offset < 0 || offset >= 256.0 * beatsPerBar)
        return fail ("start_beat must be between 0 and the 256-bar song limit");

    // Collect notes per MIDI track (beats from the file start).
    struct Part { juce::String name; juce::Array<juce::var> notes; };
    std::vector<Part> parts;
    double fileEndBeat = 0;

    for (int ti = 0; ti < file.getNumTracks(); ++ti)
    {
        juce::MidiMessageSequence seq (*file.getTrack (ti));
        seq.updateMatchedPairs();
        Part part { f.getFileNameWithoutExtension() + (file.getNumTracks() > 2 ? " " + juce::String (ti) : juce::String()), {} };

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* e = seq.getEventPointer (i);

            if (e->message.isTrackNameEvent())
                part.name = e->message.getTextFromTextMetaEvent();

            if (! e->message.isNoteOn() || e->noteOffObject == nullptr)
                continue;

            const double start = e->message.getTimeStamp() / ticksPerQuarter + offset;
            const double end = e->noteOffObject->message.getTimeStamp() / ticksPerQuarter + offset;
            fileEndBeat = juce::jmax (fileEndBeat, end);

            part.notes.add (makeObject ({ { "pitch", e->message.getNoteNumber() }, { "start", start },
                                          { "length", juce::jmax (0.05, end - start) },
                                          { "velocity", (int) e->message.getVelocity() } }));
        }

        if (! part.notes.isEmpty())
            parts.push_back (std::move (part));
    }

    if (parts.empty())
        return fail ("The MIDI file has no notes");

    if (fileEndBeat > 256.0 * beatsPerBar)
        return fail ("The MIDI file would run past the 256-bar song limit at that position");

    // Grow the song if the file is longer.
    const int barsNeeded = (int) std::ceil (fileEndBeat / beatsPerBar);
    if (barsNeeded > getBars())
    {
        edit->state.setProperty ("claudeDawBars", juce::jmin (256, barsNeeded), &edit->getUndoManager());
        updateLoopRange();
    }

    juce::Array<juce::var> touched;

    if (input.hasProperty ("track") && (int) input["track"] >= 0)
    {
        auto track = getTrack (input);
        if (track == nullptr)
            return fail ("No such track");

        for (auto& p : parts)
            writeTrackNotes (*track, p.notes, false);

        touched.add ((int) input["track"]);
    }
    else
    {
        juce::String error;
        for (auto& p : parts)
        {
            auto track = createTrack (p.name, nullptr);
            if (track == nullptr)
                continue;
            useDefaultSynth (*track, "Just A Pad");

            for (auto c : getMidiClips (*track))
                c->removeFromParent();

            writeTrackNotes (*track, p.notes, false);
            touched.add (getTracks().indexOf (track));
        }
    }

    int noteCount = 0;
    for (auto& p : parts)
        noteCount += p.notes.size();

    return ok ({ { "tracks", touched }, { "notes", noteCount }, { "bars", getBars() } });
}
