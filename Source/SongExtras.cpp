// Arrangement, mixing and performance tools for Song (see Song.h).
#include "Song.h"

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

    const juce::Identifier sectionsProperty ("claudeDawSections");
    const juce::Identifier keyProperty ("claudeDawKey");
    const juce::Identifier takesProperty ("claudeDawTakes");

    struct Note { int pitch; double start, length; int velocity; };

    juce::var noteToVar (const Note& n)
    {
        return makeObject ({ { "pitch", n.pitch }, { "start", n.start }, { "length", n.length }, { "velocity", n.velocity } });
    }

    const std::map<juce::String, std::vector<int>> scales {
        { "major",          { 0, 2, 4, 5, 7, 9, 11 } },
        { "minor",          { 0, 2, 3, 5, 7, 8, 10 } },
        { "dorian",         { 0, 2, 3, 5, 7, 9, 10 } },
        { "phrygian",       { 0, 1, 3, 5, 7, 8, 10 } },
        { "lydian",         { 0, 2, 4, 6, 7, 9, 11 } },
        { "mixolydian",     { 0, 2, 4, 5, 7, 9, 10 } },
        { "harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } },
        { "minor pentatonic", { 0, 3, 5, 7, 10 } },
        { "major pentatonic", { 0, 2, 4, 7, 9 } },
        { "blues",          { 0, 3, 5, 6, 7, 10 } },
    };

    const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

//==============================================================================
// Notes helpers

static std::vector<Note> readNotes (const std::vector<Song::NoteData>& data)
{
    std::vector<Note> notes;
    for (auto& n : data)
        notes.push_back ({ n.pitch, n.start, n.length, n.velocity });
    return notes;
}

juce::Array<int> Song::trackListFrom (const juce::var& input)
{
    juce::Array<int> result;

    if (auto* arr = input["tracks"].getArray())
    {
        for (auto& t : *arr)
            result.addIfNotAlreadyThere ((int) t);
    }
    else if (input.hasProperty ("track"))
    {
        result.add ((int) input["track"]);
    }
    else
    {
        for (int i = 0; i < getTracks().size(); ++i)
            result.add (i);
    }

    return result;
}

//==============================================================================
// Master bus effects and buses

te::PluginList* Song::getPluginListFor (int trackIndex)
{
    if (trackIndex < 0)
        return &edit->getMasterPluginList();

    if (auto t = getTracks()[trackIndex])
        return &t->pluginList;

    return nullptr;
}

juce::Array<te::Plugin*> Song::getMasterEffects()
{
    juce::Array<te::Plugin*> effects;

    for (auto p : edit->getMasterPluginList().getPlugins())
        if (dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr)
            effects.add (p);

    return effects;
}

te::AuxReturnPlugin* Song::getAuxReturn (te::AudioTrack& track)
{
    for (auto p : track.pluginList.getPlugins())
        if (auto r = dynamic_cast<te::AuxReturnPlugin*> (p))
            return r;

    return nullptr;
}

juce::var Song::toolAddBus (const juce::var& input)
{
    // Find a free bus number.
    juce::Array<int> used;
    for (auto t : getTracks())
        if (auto r = getAuxReturn (*t))
            used.add (r->busNumber.get());

    int bus = 0;
    while (used.contains (bus) && bus < 31)
        ++bus;

    if (used.contains (bus))
        return fail ("No free buses (32 max)");

    auto name = input.getProperty ("name", "Bus " + juce::String (bus + 1)).toString();
    auto track = createTrack (name, nullptr);

    if (track == nullptr)
        return fail ("Could not create bus track");

    auto ret = edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
    if (auto r = dynamic_cast<te::AuxReturnPlugin*> (ret.get()))
        r->busNumber = bus;
    track->pluginList.insertPlugin (ret, 0, nullptr);
    edit->setAuxBusName (bus, name);

    auto result = ok ({ { "track", getTracks().indexOf (track) }, { "bus", bus }, { "name", name } });

    if (input.hasProperty ("effect"))
    {
        auto fxInput = makeObject ({ { "track", getTracks().indexOf (track) }, { "effect", input["effect"] } });
        if (input.hasProperty ("params"))
            fxInput.getDynamicObject()->setProperty ("params", input["params"]);
        result.getDynamicObject()->setProperty ("effect", toolAddEffect (fxInput));
    }

    return result;
}

juce::var Song::toolSetSend (const juce::var& input)
{
    auto track = getTrack (input);
    auto busTrack = getTracks()[(int) input.getProperty ("bus_track", -1)];

    if (track == nullptr || busTrack == nullptr)
        return fail ("track and bus_track must be valid track indices");

    auto ret = getAuxReturn (*busTrack);
    if (ret == nullptr)
        return fail (busTrack->getName() + " is not a bus. Create one with add_bus.");

    const int bus = ret->busNumber.get();
    const float levelDb = (float) (double) input.getProperty ("level_db", -6.0);

    te::AuxSendPlugin* send = nullptr;
    for (auto p : track->pluginList.getPlugins())
        if (auto s = dynamic_cast<te::AuxSendPlugin*> (p))
            if (s->getBusNumber() == bus)
                send = s;

    if (levelDb <= -60.0f)
    {
        if (send != nullptr)
            send->deleteFromParent();
        return ok ({ { "removed", true } });
    }

    if (send == nullptr)
    {
        auto p = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
        send = dynamic_cast<te::AuxSendPlugin*> (p.get());
        send->busNumber = bus;

        // Post-fader: after the track's volume, before its meter.
        auto at = track->pluginList.indexOf (track->getVolumePlugin());
        track->pluginList.insertPlugin (p, at < 0 ? track->pluginList.size() : at + 1, nullptr);
    }

    send->setGainDb (juce::jlimit (-60.0f, 6.0f, levelDb));
    return ok ({ { "bus", busTrack->getName() }, { "level_db", send->getGainDb() } });
}

juce::var Song::describeSends (te::AudioTrack& track)
{
    juce::Array<juce::var> sends;

    for (auto p : track.pluginList.getPlugins())
        if (auto s = dynamic_cast<te::AuxSendPlugin*> (p))
        {
            int busTrackIndex = -1, i = 0;
            for (auto t : getTracks())
            {
                if (auto r = getAuxReturn (*t); r != nullptr && r->busNumber.get() == s->getBusNumber())
                    busTrackIndex = i;
                ++i;
            }
            sends.add (makeObject ({ { "bus_track", busTrackIndex }, { "level_db", s->getGainDb() } }));
        }

    return sends;
}

//==============================================================================
// Sidechain

juce::var Song::toolSetSidechain (const juce::var& input)
{
    auto device = findDevice ((int) input["track"], input["device"]);

    if (device == nullptr)
        return fail ("No such device");

    const int source = input.getProperty ("source_track", -1);

    if (source < 0)
    {
        device->setSidechainSourceByName ({});
        if (auto c = dynamic_cast<te::CompressorPlugin*> (device))
            c->useSidechainTrigger = false;
        return ok ({ { "sidechain", "none" } });
    }

    const auto prefix = juce::String (source + 1) + ". ";
    juce::String match;
    for (auto& n : device->getSidechainSourceNames (false))
        if (n.startsWith (prefix))
            match = n;

    if (match.isEmpty())
        return fail ("Source must be another track (and not the device's own track)");

    device->setSidechainSourceByName (match);
    device->guessSidechainRouting();

    if (auto c = dynamic_cast<te::CompressorPlugin*> (device))
        c->useSidechainTrigger = true;

    return ok ({ { "sidechain", match } });
}

juce::var Song::toolSidechainDuck (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    const auto& src = input["source_track"];
    if (! (src.isInt() || src.isInt64() || src.isDouble()) || (int) src == (int) input["track"] || getTracks()[(int) src] == nullptr)
        return fail ("source_track must be another track's index");

    const double amount = juce::jlimit (0.0, 1.0, (double) input.getProperty ("amount", 0.7));

    auto fx = toolAddEffect (makeObject ({ { "track", input["track"] }, { "effect", "Compressor" } }));
    if (! (bool) fx["ok"])
        return fx;

    const int device = fx["device"];
    auto comp = getEffects (*track)[device];

    // Deeper ducking: lower threshold, higher ratio. Fast attack, musical release.
    applyParams (*comp, makeObject ({ { "threshold", juce::jmap (amount, 0.5, 0.03) },
                                      { "ratio", juce::jmap (amount, 0.5, 0.08) },
                                      { "attack", 1.0 },
                                      { "release", (double) input.getProperty ("release_ms", 150.0) } }));

    auto sc = toolSetSidechain (makeObject ({ { "track", input["track"] }, { "device", device },
                                              { "source_track", input["source_track"] } }));
    if (! (bool) sc["ok"])
    {
        comp->deleteFromParent();   // don't leave a half-configured compressor behind
        return sc;
    }

    return ok ({ { "device", device }, { "sidechain", sc["sidechain"] }, { "amount", amount } });
}

//==============================================================================
// Audio clips

juce::var Song::toolAddAudioClip (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    const juce::File f (input["path"].toString());
    te::AudioFile audio (engine, f);
    if (! f.existsAsFile() || ! audio.isValid())
        return fail ("Not a readable audio file: " + f.getFullPathName());

    const double startBeat = input.getProperty ("start_beat", 0.0);
    if (startBeat < 0 || startBeat >= getSongBeats())
        return fail ("start_beat must be inside the song (0 to " + juce::String (getSongBeats()) + ")");
    if (input.hasProperty ("length_beats") && (double) input["length_beats"] <= 0)
        return fail ("length_beats must be greater than 0");
    const auto start = beatToTime (startBeat);
    const double sourceBpm = input.getProperty ("source_bpm", 0.0);
    const bool loop = input.getProperty ("loop", false);

    te::TimePosition end;
    if (input.hasProperty ("length_beats"))
        end = beatToTime (startBeat + (double) input["length_beats"]);
    else
        end = start + te::TimeDuration::fromSeconds (audio.getLength());

    auto clip = track->insertWaveClip (f.getFileNameWithoutExtension(), f, { { start, end }, {} }, false);
    if (clip == nullptr)
        return fail ("Could not insert clip");

    // Read the source file directly; waiting on a generated proxy stalls offline renders.
    clip->setUsesProxy (false);
    // Absolute path: stays valid after Save As to another folder and after crash recovery.
    clip->getSourceFileReference().setToFile (f, te::SourceFileReference::PathStyle::alwaysAbsolute, false);

    if (sourceBpm > 0)
    {
        // Follow the song tempo, and loop over the file's length in beats.
        clip->setAutoTempo (true);
        if (loop)
        {
            const double fileBeats = audio.getLength() * sourceBpm / 60.0;
            clip->setLoopRangeBeats ({ te::BeatPosition(), te::BeatDuration::fromBeats (fileBeats) });
        }
    }

    if (input.hasProperty ("gain_db"))
        clip->setGainDB ((float) (double) input["gain_db"]);

    return ok ({ { "clip", track->getClips().indexOf (clip.get()) }, { "name", clip->getName() },
                 { "note", getInstrument (*track) != nullptr ? "This track also has an instrument; audio clips play best on a track with instrument None." : "" } });
}

juce::var Song::toolRemoveAudioClip (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    auto clip = dynamic_cast<te::WaveAudioClip*> (track->getClips()[(int) input["clip"]]);
    if (clip == nullptr)
        return fail ("No audio clip at that index (see audioClips in get_song)");

    auto name = clip->getName();
    clip->removeFromParent();
    return ok ({ { "removed", name } });
}

juce::var Song::describeAudioClips (te::AudioTrack& track)
{
    juce::Array<juce::var> clips;
    int index = 0;

    for (auto c : track.getClips())
    {
        if (auto w = dynamic_cast<te::WaveAudioClip*> (c))
        {
            auto range = w->getEditTimeRange();
            clips.add (makeObject ({ { "clip", index },
                                     { "name", w->getName() },
                                     { "file", w->getOriginalFile().getFullPathName() },
                                     { "start_beat", edit->tempoSequence.toBeats (range.getStart()).inBeats() },
                                     { "end_beat", edit->tempoSequence.toBeats (range.getEnd()).inBeats() },
                                     { "looping", w->isLooping() } }));
        }
        ++index;
    }

    return clips;
}

//==============================================================================
// Arrangement

juce::var Song::toolSetSections (const juce::var& input)
{
    auto* arr = input["sections"].getArray();
    if (arr == nullptr)
        return fail ("sections must be an array of {name, start_bar, end_bar}");

    juce::Array<juce::var> clean;
    for (auto& s : *arr)
    {
        const int a = s["start_bar"], b = s["end_bar"];
        if (a >= 1 && b > a)
            clean.add (makeObject ({ { "name", s["name"].toString() }, { "start_bar", a }, { "end_bar", b } }));
    }

    edit->state.setProperty (sectionsProperty, juce::JSON::toString (clean, true), &edit->getUndoManager());
    return ok ({ { "sections", clean.size() } });
}

juce::var Song::getSections()
{
    auto v = juce::JSON::parse (edit->state.getProperty (sectionsProperty).toString());
    return v.isArray() ? v : juce::var (juce::Array<juce::var>());
}

juce::var Song::toolCopyBars (const juce::var& input)
{
    const int from = input["from_bar"], to = input["to_bar"], dest = input["dest_bar"];
    const int transpose = input.getProperty ("transpose", 0);

    if (from < 1 || to <= from || dest < 1)
        return fail ("Need from_bar >= 1, to_bar > from_bar (exclusive end), dest_bar >= 1");

    const double srcStart = (from - 1) * 4.0, srcEnd = (to - 1) * 4.0;
    const double offset = (dest - from) * 4.0;
    const double destStart = srcStart + offset, destEnd = srcEnd + offset;

    if (destEnd > getSongBeats())
        return fail ("Destination runs past the song end; call set_song_length first");

    int copied = 0;
    for (int ti : trackListFrom (input))
    {
        auto track = getTracks()[ti];
        auto clip = track;
        if (clip == nullptr)
            continue;

        auto notes = readNotes (readTrackNotes (*clip));

        // Clear the destination range first (unless merging).
        if (! (bool) input.getProperty ("merge", false))
            toolRemoveNotes (makeObject ({ { "track", ti }, { "start_beat", destStart }, { "end_beat", destEnd } }));

        juce::Array<juce::var> toAdd;
        for (auto& n : notes)
            if (n.start >= srcStart && n.start < srcEnd)
                toAdd.add (noteToVar ({ juce::jlimit (0, 127, n.pitch + transpose), n.start + offset,
                                        juce::jmin (n.length, srcEnd - n.start), n.velocity }));

        writeTrackNotes (*clip, toAdd, false);
        copied += toAdd.size();
    }

    return ok ({ { "notes_copied", copied } });
}

juce::var Song::toolClearBars (const juce::var& input)
{
    const int from = input["from_bar"], to = input["to_bar"];
    if (from < 1 || to <= from)
        return fail ("Need from_bar >= 1 and to_bar > from_bar (exclusive end)");

    int removed = 0;
    for (int ti : trackListFrom (input))
        removed += (int) toolRemoveNotes (makeObject ({ { "track", ti }, { "start_beat", (from - 1) * 4.0 },
                                                         { "end_beat", (to - 1) * 4.0 } }))["removed"];

    return ok ({ { "removed", removed } });
}

//==============================================================================
// Groove and pitch

juce::var Song::toolGroove (const juce::var& input)
{
    auto track = getTrack (input);
    auto clip = track;
    if (clip == nullptr)
        return fail ("No such track");

    const double from = input.getProperty ("start_beat", 0.0);
    const double to = input.getProperty ("end_beat", getSongBeats());
    const double quantize = input.getProperty ("quantize", 0.0);         // grid in beats, 0 = off
    const double swing = juce::jlimit (0.0, 1.0, (double) input.getProperty ("swing", 0.0));
    const double swingGrid = input.getProperty ("swing_grid", 0.5);     // 0.5 = 8ths, 0.25 = 16ths
    if (swingGrid <= 0)
        return fail ("swing_grid must be greater than 0 (0.5 = 8ths, 0.25 = 16ths)");
    const double timing = juce::jlimit (0.0, 0.1, (double) input.getProperty ("humanize_timing", 0.0));
    const double velocity = juce::jlimit (0.0, 1.0, (double) input.getProperty ("humanize_velocity", 0.0));

    juce::Random rng;
    auto notes = readNotes (readTrackNotes (*clip));
    juce::Array<juce::var> out;
    int changed = 0;

    for (auto n : notes)
    {
        const auto original = n;

        if (n.start >= from && n.start < to && ! isBeatLocked (*track, n.start))
        {
            if (quantize > 0)
                n.start = std::round (n.start / quantize) * quantize;

            // Delay notes on the off-beat of the swing grid by up to half a grid step.
            const double pos = n.start / swingGrid;
            if (swing > 0 && std::abs (pos - std::round (pos)) < 1.0e-3 && ((int) std::round (pos)) % 2 == 1)
                n.start += swing * swingGrid * 0.5;

            if (timing > 0)
                n.start = juce::jmax (0.0, n.start + (rng.nextDouble() * 2.0 - 1.0) * timing);

            if (velocity > 0)
                n.velocity = juce::jlimit (1, 127, juce::roundToInt (n.velocity * (1.0 + (rng.nextDouble() * 2.0 - 1.0) * velocity * 0.5)));

            // Moving into locked bars would get the note refused (and lost); leave it be.
            if (isBeatLocked (*track, n.start))
                n = original;
            else
                ++changed;
        }
        out.add (noteToVar (n));
    }

    writeTrackNotes (*clip, out, true);
    return ok ({ { "notes_changed", changed } });
}

juce::var Song::toolTranspose (const juce::var& input)
{
    const int semitones = input["semitones"];
    const double from = input.getProperty ("start_beat", 0.0);
    const double to = input.getProperty ("end_beat", getSongBeats());
    int changed = 0;

    for (int ti : trackListFrom (input))
    {
        auto track = getTracks()[ti];
        auto clip = track;
        if (clip == nullptr)
            continue;

        juce::Array<juce::var> out;
        for (auto n : readNotes (readTrackNotes (*clip)))
        {
            if (n.start >= from && n.start < to)
            {
                n.pitch = juce::jlimit (0, 127, n.pitch + semitones);
                ++changed;
            }
            out.add (noteToVar (n));
        }
        writeTrackNotes (*clip, out, true);
    }

    return ok ({ { "notes_changed", changed } });
}

juce::var Song::toolSetKey (const juce::var& input)
{
    auto root = input["root"].toString().trim();

    if (root.isEmpty() || root == "-")
    {
        edit->state.removeProperty (keyProperty, &edit->getUndoManager());
        return ok ({ { "key", "none" } });
    }
    auto scale = input.getProperty ("scale", "minor").toString().toLowerCase().trim();

    root = root.substring (0, 1).toUpperCase() + root.substring (1);
    static const std::map<juce::String, juce::String> flatToSharp { { "Db", "C#" }, { "Eb", "D#" }, { "Gb", "F#" }, { "Ab", "G#" }, { "Bb", "A#" } };
    if (auto it = flatToSharp.find (root); it != flatToSharp.end())
        root = it->second;

    if (! noteNames.contains (root))
        return fail ("root must be a note name like A, C#, Eb");

    if (scales.find (scale) == scales.end())
    {
        juce::StringArray names;
        for (auto& [k, v] : scales) names.add (k);
        return fail ("scale must be one of: " + names.joinIntoString (", "));
    }

    edit->state.setProperty (keyProperty, root + " " + scale, &edit->getUndoManager());
    return ok ({ { "key", root + " " + scale } });
}

juce::var Song::getKeyInfo()
{
    auto key = edit->state.getProperty (keyProperty).toString();
    if (key.isEmpty())
        return {};

    auto root = key.upToFirstOccurrenceOf (" ", false, false);
    auto scale = key.fromFirstOccurrenceOf (" ", false, false);
    juce::Array<juce::var> pcs;

    if (auto it = scales.find (scale); it != scales.end())
        for (int step : it->second)
            pcs.add ((noteNames.indexOf (root) + step) % 12);

    return makeObject ({ { "name", key }, { "root", root }, { "scale", scale }, { "pitch_classes", pcs } });
}

//==============================================================================
// Takes (variations)

juce::var Song::readTakes (te::AudioTrack& track)
{
    auto v = juce::JSON::parse (track.state.getProperty (takesProperty).toString());
    if (! v.isObject())
        v = makeObject ({ { "active", "" }, { "takes", juce::var (new juce::DynamicObject()) } });
    return v;
}

void Song::writeTakes (te::AudioTrack& track, const juce::var& takes)
{
    track.state.setProperty (takesProperty, juce::JSON::toString (takes, true), &edit->getUndoManager());
}

juce::var Song::toolSaveTake (const juce::var& input)
{
    auto track = getTrack (input);
    auto clip = track;
    if (clip == nullptr)
        return fail ("No such track");

    auto name = input["name"].toString().trim();
    if (name.isEmpty())
        return fail ("name is required");

    juce::Array<juce::var> notes;
    if (auto* given = input["notes"].getArray())
        notes = *given;
    else
        for (auto& n : readNotes (readTrackNotes (*clip)))
            notes.add (noteToVar (n));

    auto takes = readTakes (*track);

    // Before switching away, keep the current notes under the active take's name.
    const bool activate = input.getProperty ("activate", input.hasProperty ("notes"));
    auto active = takes["active"].toString();
    if (activate && active.isNotEmpty() && active != name)
    {
        juce::Array<juce::var> current;
        for (auto& n : readNotes (readTrackNotes (*clip)))
            current.add (noteToVar (n));
        takes["takes"].getDynamicObject()->setProperty (active, current);
    }

    takes["takes"].getDynamicObject()->setProperty (name, notes);

    if (activate || active.isEmpty())
    {
        takes.getDynamicObject()->setProperty ("active", name);
        if (activate)
            writeTrackNotes (*clip, notes, true);
    }

    writeTakes (*track, takes);
    return ok ({ { "take", name }, { "active", takes["active"] }, { "notes", notes.size() } });
}

juce::var Song::toolUseTake (const juce::var& input)
{
    auto track = getTrack (input);
    auto clip = track;
    if (clip == nullptr)
        return fail ("No such track");

    auto name = input["name"].toString();
    auto takes = readTakes (*track);
    auto* store = takes["takes"].getDynamicObject();

    if (store == nullptr || ! store->hasProperty (name))
        return fail ("No take named '" + name + "'");

    auto active = takes["active"].toString();
    if (active.isNotEmpty() && active != name)
    {
        juce::Array<juce::var> current;
        for (auto& n : readNotes (readTrackNotes (*clip)))
            current.add (noteToVar (n));
        store->setProperty (active, current);
    }

    if (auto* notes = store->getProperty (name).getArray())
        writeTrackNotes (*clip, *notes, true);

    takes.getDynamicObject()->setProperty ("active", name);
    writeTakes (*track, takes);
    return ok ({ { "active", name } });
}

juce::var Song::toolDeleteTake (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    auto takes = readTakes (*track);
    auto name = input["name"].toString();
    if (auto* store = takes["takes"].getDynamicObject())
        store->removeProperty (name);
    if (takes["active"].toString() == name)
        takes.getDynamicObject()->setProperty ("active", "");

    writeTakes (*track, takes);
    return ok();
}

juce::var Song::describeTakes (te::AudioTrack& track)
{
    auto takes = readTakes (track);
    juce::StringArray names;
    if (auto* store = takes["takes"].getDynamicObject())
        for (auto& p : store->getProperties())
            names.add (p.name.toString());

    if (names.isEmpty())
        return {};

    juce::Array<juce::var> list;
    for (auto& n : names) list.add (n);
    return makeObject ({ { "active", takes["active"] }, { "names", list } });
}

//==============================================================================
// Drum sounds

juce::var Song::describeSamplerSounds (te::AudioTrack& track)
{
    juce::Array<juce::var> sounds;

    if (auto s = dynamic_cast<te::SamplerPlugin*> (getInstrument (track)))
        for (int i = 0; i < s->getNumSounds(); ++i)
            sounds.add (makeObject ({ { "name", s->getSoundName (i) }, { "key", s->getKeyNote (i) },
                                      { "min_key", s->getMinKey (i) }, { "max_key", s->getMaxKey (i) } }));

    return sounds;
}
