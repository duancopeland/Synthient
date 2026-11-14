#include "Song.h"
#include "SynthSettings.h"
#include "Sampler/ClaudeSamplerPlugin.h"

using namespace std::literals;

namespace
{
    const juce::Identifier barsProperty ("claudeDawBars");
    constexpr int defaultBars = 8;
    constexpr int maxParamsListed = 128;

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

    const juce::Array<juce::Colour> trackColours {
        juce::Colour (0xffff4fa3), juce::Colour (0xff4fd1ff), juce::Colour (0xffffce54),
        juce::Colour (0xff8bff6a), juce::Colour (0xffb38bff), juce::Colour (0xffff8a4f),
    };

    struct BuiltIn { const char* name; const char* type; const char* description; };

    const BuiltIn builtInInstruments[] = {
        { "4OSC",    te::FourOscPlugin::xmlTypeName, "Built-in 4-oscillator subtractive synth (basses, pads, leads, plucks). Shape it with set_device_params." },
        { "Sampler", te::SamplerPlugin::xmlTypeName, "Built-in sampler. Load audio files with load_sample and map them to keys (drums, one-shots, chopped loops)." },
        { "Instrument", ClaudeSamplerPlugin::xmlTypeName, "Multi-sample instrument player (real strings, piano, guitars...). Don't pick it here: use find_instruments + load_instrument." },
    };

    const BuiltIn builtInEffects[] = {
        { "Reverb",      te::ReverbPlugin::xmlTypeName,     "Room/plate style reverb" },
        { "Delay",       te::DelayPlugin::xmlTypeName,      "Feedback delay / echo" },
        { "EQ",          te::EqualiserPlugin::xmlTypeName,  "4-band parametric equaliser" },
        { "Compressor",  te::CompressorPlugin::xmlTypeName, "Compressor / limiter" },
        { "Chorus",      te::ChorusPlugin::xmlTypeName,     "Chorus (classic 80s width)" },
        { "Phaser",      te::PhaserPlugin::xmlTypeName,     "Phaser" },
        { "Filter",      te::LowPassPlugin::xmlTypeName,    "Low-pass / high-pass filter" },
        { "Pitch Shift", te::PitchShiftPlugin::xmlTypeName, "Pitch shifter" },
    };
}

//==============================================================================
Song::Song (te::Engine& e) : engine (e)
{
    newSong();
}

Song::~Song()
{
    *alive = false;
    renderHandle = nullptr;
}

void Song::newSong()
{
    renderHandle = nullptr; // cancels any render that still uses the old Edit

    if (edit != nullptr)
        edit->getTransport().stop (false, true);

    edit = std::make_unique<te::Edit> (engine, te::Edit::EditRole::forEditing);
    file = juce::File();

    for (auto t : getTracks())
        edit->deleteTrack (t);

    edit->state.setProperty (barsProperty, defaultBars, nullptr);
    edit->tempoSequence.getTempo (0)->setBpm (100.0);

    for (auto [name, patch] : { std::pair { "Bass", "Retrowaver" }, { "Chords", "OB One" }, { "Lead", "Smother Lead" } })
        if (auto t = createTrack (name, nullptr))
            useDefaultSynth (*t, patch);

    initialiseEdit();
}

juce::String Song::load (const juce::File& f)
{
    auto loaded = te::loadEditFromFile (engine, f);

    if (loaded == nullptr)
        return "Could not load " + f.getFullPathName();

    renderHandle = nullptr;

    if (edit != nullptr)
        edit->getTransport().stop (false, true);

    edit = std::move (loaded);
    file = f;

    if (! edit->state.hasProperty (barsProperty))
        edit->state.setProperty (barsProperty, defaultBars, nullptr);

    initialiseEdit();
    return {};
}

juce::String Song::saveCopy (const juce::File& f)
{
    edit->flushState();
    return te::EditFileOperations (*edit).writeToFile (f, false) ? juce::String() : "Could not write " + f.getFullPathName();
}

juce::String Song::save (const juce::File& f)
{
    edit->flushState();

    if (! te::EditFileOperations (*edit).writeToFile (f, false))
        return "Could not write " + f.getFullPathName();

    file = f;
    return {};
}

void Song::initialiseEdit()
{
    // Audio clip sources are stored as paths; resolve them against the song file when it
    // has one, and accept absolute paths for unsaved songs (otherwise readers come back null).
    edit->editFileRetriever = [this] { return file; };
    edit->filePathResolver = [this] (const juce::String& path) -> juce::File
    {
        if (juce::File::isAbsolutePath (path))
            return juce::File (path);

        return file != juce::File() ? file.getParentDirectory().getChildFile (path) : juce::File();
    };

    edit->playInStopEnabled = true;
    updateLoopRange();
    edit->getTransport().looping = true;
    edit->getUndoManager().clearUndoHistory();
}

int Song::getBars()
{
    return juce::jlimit (1, 256, (int) edit->state.getProperty (barsProperty, defaultBars));
}

void Song::updateLoopRange()
{
    edit->getTransport().setLoopRange (te::TimeRange (0s, edit->tempoSequence.toTime ({ getBars(), {} })));
}

//==============================================================================
juce::Array<te::AudioTrack*> Song::getTracks()
{
    return te::getAudioTracks (*edit);
}

te::AudioTrack* Song::getTrack (const juce::var& input)
{
    if (! input.hasProperty ("track"))
        return nullptr;

    const auto& v = input["track"];
    if (! (v.isInt() || v.isInt64() || v.isDouble()))
        return nullptr;

    return getTracks()[(int) input["track"]];
}

te::Plugin* Song::getInstrument (te::AudioTrack& track)
{
    for (auto p : track.pluginList.getPlugins())
        if (p->isSynth())
            return p;

    return nullptr;
}

juce::Array<te::Plugin*> Song::getEffects (te::AudioTrack& track)
{
    juce::Array<te::Plugin*> effects;
    auto instrument = getInstrument (track);

    for (auto p : track.pluginList.getPlugins())
        if (p != instrument
             && dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr
             && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr
             && dynamic_cast<te::AuxSendPlugin*> (p) == nullptr
             && dynamic_cast<te::AuxReturnPlugin*> (p) == nullptr)
            effects.add (p);

    return effects;
}

te::Plugin* Song::findDevice (int trackIndex, const juce::var& device)
{
    if (trackIndex < 0)
        return device.toString() == "instrument" ? nullptr : getMasterEffects()[(int) device];

    auto track = getTracks()[trackIndex];

    if (track == nullptr)
        return nullptr;

    if (device.toString() == "instrument")
        return getInstrument (*track);

    if (device.isInt() || device.isDouble() || device.toString().containsOnly ("0123456789"))
        return getEffects (*track)[(int) device];

    return nullptr;
}

te::AudioTrack* Song::createTrack (const juce::String& name, te::Plugin::Ptr instrument)
{
    auto tracks = getTracks();
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, tracks.getLast()), nullptr);

    if (track == nullptr)
        return nullptr;

    track->setName (name);
    track->setColour (trackColours[tracks.size() % trackColours.size()]);

    if (instrument != nullptr)
        track->pluginList.insertPlugin (instrument, 0, nullptr);

    createMidiClip (*track, 0.0, getSongBeats(), name);
    return track.get();
}

//==============================================================================
juce::File Song::findSynthPatch (const juce::String& name)
{
    // OB-Xf first: the default sounds are OB-Xf patches (warmer than most Surge ones).
    static juce::var indexes[2];
    auto root = ClaudeSamplerPlugin::libraryRoot ? ClaudeSamplerPlugin::libraryRoot() : juce::File();
    const char* files[] = { "obxf_patches.json", "surge_patches.json" };

    for (int i = 0; i < 2; ++i)
    {
        if (! indexes[i].isObject())
            indexes[i] = juce::JSON::parse (root.getChildFile (files[i]));

        if (auto* list = indexes[i]["patches"].getArray())
            for (auto& p : *list)
                if (p["name"].toString().equalsIgnoreCase (name))
                    return root.getChildFile (p["path"].toString());
    }

    return {};
}

void Song::useDefaultSynth (te::AudioTrack& track, const juce::String& patchName)
{
    auto patch = findSynthPatch (patchName);

    if (patch.existsAsFile())
    {
        auto input = new juce::DynamicObject();
        input->setProperty ("track", getTracks().indexOf (&track));
        input->setProperty ("path", patch.getFullPathName());
        input->setProperty ("name", patchName);

        if ((bool) toolLoadSynthPatch (juce::var (input))["ok"])
            return;
    }

    juce::String error;
    if (auto osc = createInstrument ("4OSC", error))
        track.pluginList.insertPlugin (osc, 0, nullptr);
}

std::optional<juce::PluginDescription> Song::findExternalPlugin (const juce::String& name, bool instrument)
{
    for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
        if (d.isInstrument == instrument && d.name.equalsIgnoreCase (name))
            return d;

    for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
        if (d.isInstrument == instrument && d.name.containsIgnoreCase (name))
            return d;

    return std::nullopt;
}

te::Plugin::Ptr Song::createInstrument (const juce::String& name, juce::String& error)
{
    if (name.equalsIgnoreCase ("None"))
        return {};

    for (auto& b : builtInInstruments)
        if (name.equalsIgnoreCase (b.name))
            return edit->getPluginCache().createNewPlugin (b.type, {});

    if (auto desc = findExternalPlugin (name, true))
    {
        auto p = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *desc);

        if (p == nullptr)
            error = "Failed to load " + desc->name;

        return p;
    }

    error = "Unknown instrument '" + name + "'. Call list_instruments.";
    return {};
}

te::Plugin::Ptr Song::createEffect (const juce::String& name, juce::String& error)
{
    for (auto& b : builtInEffects)
        if (name.equalsIgnoreCase (b.name))
            return edit->getPluginCache().createNewPlugin (b.type, {});

    if (auto desc = findExternalPlugin (name, false))
    {
        auto p = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *desc);

        if (p == nullptr)
            error = "Failed to load " + desc->name;

        return p;
    }

    error = "Unknown effect '" + name + "'. Call list_effects.";
    return {};
}

juce::var Song::describeDevice (te::Plugin& p, int index)
{
    auto obj = makeObject ({ { "index", index },
                             { "name", p.getName() },
                             { "external", dynamic_cast<te::ExternalPlugin*> (&p) != nullptr },
                             { "enabled", p.isEnabled() } });

    if (p.state.hasProperty ("claudePatch"))
        obj.getDynamicObject()->setProperty ("patch", p.state["claudePatch"]);

    if (auto inst = dynamic_cast<ClaudeSamplerPlugin*> (&p))
    {
        auto* o = obj.getDynamicObject();
        o->setProperty ("library_instrument", inst->getInstrumentName());
        o->setProperty ("regions", inst->getNumRegions());
        auto range = inst->getKeyRange();
        if (! range.isEmpty())
            o->setProperty ("range", juce::Array<juce::var> { range.getStart(), range.getEnd() });
        juce::Array<juce::var> ks;
        for (auto k : inst->getKeyswitches())
            ks.add (k);
        if (! ks.isEmpty())
            o->setProperty ("keyswitches", ks);
        if (inst->getLoadError().isNotEmpty())
            o->setProperty ("error", inst->getLoadError());
    }

    // Sidechain source as a track index ("3. Drums" -> 2), or -1.
    if (p.canSidechain())
    {
        auto src = p.getSidechainSourceName();
        obj.getDynamicObject()->setProperty ("sidechain_track", src.containsChar ('.') ? src.getIntValue() - 1 : -1);
    }

    return obj;
}

juce::var Song::describeParams (te::Plugin& p, const juce::String& filter, int limit)
{
    juce::Array<juce::var> params;
    int total = 0;

    // 4OSC: waveforms, unison, filter type, switches and mod routings come first.
    if (auto osc = dynamic_cast<te::FourOscPlugin*> (&p))
        for (auto& s : SynthSettings::describe (*osc, filter))
        {
            ++total;
            if (params.size() < limit)
                params.add (s);
        }

    for (auto param : p.getAutomatableParameters())
    {
        if (filter.isNotEmpty() && ! param->getParameterName().containsIgnoreCase (filter)
                                && ! param->paramID.containsIgnoreCase (filter))
            continue;

        ++total;

        if (params.size() >= limit)
            continue;

        auto range = param->getValueRange();
        params.add (makeObject ({ { "id", param->paramID },
                                  { "name", param->getParameterName() },
                                  { "min", range.getStart() },
                                  { "max", range.getEnd() },
                                  { "value", param->getCurrentValue() },
                                  { "display", param->getCurrentValueAsStringWithLabel() } }));
    }

    return makeObject ({ { "device", p.getName() }, { "params", params }, { "total_matching", total } });
}

juce::var Song::applyParams (te::Plugin& p, const juce::var& params)
{
    auto* obj = params.getDynamicObject();

    if (obj == nullptr)
        return fail ("params must be an object of {param id or name: value}");

    juce::StringArray applied, unknown;

    for (auto& prop : obj->getProperties())
    {
        const auto key = prop.name.toString();
        te::AutomatableParameter* match = nullptr;

        if (! (prop.value.isInt() || prop.value.isDouble() || prop.value.isInt64() || prop.value.isBool()))
        {
            unknown.add (key + " (value must be a number)");
            continue;
        }

        if (auto osc = dynamic_cast<te::FourOscPlugin*> (&p))
        {
            juce::String text;
            if (SynthSettings::apply (*osc, key, (double) prop.value, text))
            {
                applied.add (text);
                continue;
            }
        }

        for (auto param : p.getAutomatableParameters())
            if (param->paramID.equalsIgnoreCase (key) || param->getParameterName().equalsIgnoreCase (key))
                { match = param; break; }

        if (match == nullptr)
        {
            unknown.add (key);
            continue;
        }

        if (! (prop.value.isInt() || prop.value.isDouble() || prop.value.isInt64() || prop.value.isBool()))
        {
            unknown.add (key + " (value must be a number)");
            continue;
        }

        auto range = match->getValueRange();
        match->setParameter (juce::jlimit (range.getStart(), range.getEnd(), (float) (double) prop.value),
                             juce::sendNotification);
        applied.add (key + " = " + match->getCurrentValueAsStringWithLabel());
    }

    if (applied.isEmpty())
        return fail ("No matching params: " + unknown.joinIntoString (", ") + ". Call get_device_params.");

    return ok ({ { "applied", applied.joinIntoString ("; ") }, { "unknown", unknown.joinIntoString (", ") } });
}

//==============================================================================
juce::var Song::getState (bool includeNotes)
{
    juce::Array<juce::var> tracks;
    int index = 0;

    for (auto t : getTracks())
    {
        auto obj = makeObject ({ { "index", index++ },
                                 { "id", t->itemID.toString() },
                                 { "name", t->getName() },
                                 { "colour", "#" + t->getColour().toDisplayString (false) },
                                 { "mute", t->isMuted (false) },
                                 { "solo", t->isSolo (false) } });
        auto* o = obj.getDynamicObject();

        if (auto vp = t->getVolumePlugin())
        {
            o->setProperty ("volumeDb", vp->getVolumeDb());
            o->setProperty ("pan", vp->getPan());
        }

        if (auto inst = getInstrument (*t))
            o->setProperty ("instrument", describeDevice (*inst, -1));

        juce::Array<juce::var> effects;
        int fx = 0;
        for (auto e : getEffects (*t))
            effects.add (describeDevice (*e, fx++));
        o->setProperty ("effects", effects);

        o->setProperty ("automation", describeAutomation (t->getAllAutomatableParams()));
        o->setProperty ("kind", getAuxReturn (*t) != nullptr ? "bus" : "track");
        if (auto r = getAuxReturn (*t)) o->setProperty ("bus", r->busNumber.get());
        o->setProperty ("sends", describeSends (*t));
        o->setProperty ("audioClips", describeAudioClips (*t));
        o->setProperty ("clips", describeMidiClips (*t));
        o->setProperty ("lock", describeLocks (*t));
        o->setProperty ("takes", describeTakes (*t));
        if (dynamic_cast<te::SamplerPlugin*> (getInstrument (*t)) != nullptr)
            o->setProperty ("sounds", describeSamplerSounds (*t));

        if (includeNotes)
        {
            juce::Array<juce::var> notes;

            for (auto& n : readTrackNotes (*t))
                notes.add (makeObject ({ { "pitch", n.pitch }, { "start", n.start },
                                         { "length", n.length }, { "velocity", n.velocity } }));

            o->setProperty ("notes", notes);
        }

        tracks.add (obj);
    }

    auto& transport = edit->getTransport();

    return makeObject ({ { "bpm", edit->tempoSequence.getTempo (0)->getBpm() },
                         { "bars", getBars() },
                         { "beatsPerBar", beatsPerBar },
                         { "playing", transport.isPlaying() },
                         { "positionBeats", edit->tempoSequence.toBeats (transport.getPosition()).inBeats() },
                         { "file", file.getFileName() },
                         { "canUndo", edit->getUndoManager().canUndo() },
                         { "canRedo", edit->getUndoManager().canRedo() },
                         { "masterEffects", [this] { juce::Array<juce::var> a; int i = 0; for (auto p : getMasterEffects()) a.add (describeDevice (*p, i++)); return a; }() },
                         { "sections", getSections() },
                         { "key", getKeyInfo() },
                         { "masterAutomation", describeAutomation (edit->getMasterVolumePlugin()->getAutomatableParameters()) },
                         { "tracks", tracks } });
}

juce::var Song::getTransport()
{
    auto& transport = edit->getTransport();
    return makeObject ({ { "playing", transport.isPlaying() },
                         { "positionBeats", edit->tempoSequence.toBeats (transport.getPosition()).inBeats() } });
}

juce::var Song::runTool (const juce::String& name, const juce::var& input)
{
    if (name == "get_song")
        return ok ({ { "song", getState ((bool) input.getProperty ("include_notes", true)) } });

    if (name == "list_instruments")   return toolListInstruments (input);
    if (name == "list_effects")       return toolListEffects (input);
    if (name == "get_device_params")  return toolGetDeviceParams (input);
    if (name == "list_programs")      return toolListPrograms (input);

    using ToolFn = juce::var (Song::*) (const juce::var&);
    static const std::map<juce::String, ToolFn> mutating {
        { "add_track",         &Song::toolAddTrack },
        { "remove_track",      &Song::toolRemoveTrack },
        { "set_track",         &Song::toolSetTrack },
        { "add_notes",         &Song::toolAddNotes },
        { "remove_notes",      &Song::toolRemoveNotes },
        { "set_tempo",         &Song::toolSetTempo },
        { "set_song_length",   &Song::toolSetSongLength },
        { "set_instrument",    &Song::toolSetInstrument },
        { "add_effect",        &Song::toolAddEffect },
        { "remove_effect",     &Song::toolRemoveEffect },
        { "set_device_params", &Song::toolSetDeviceParams },
        { "load_sample",       &Song::toolLoadSample },
        { "load_sfz",          &Song::toolLoadSfz },
        { "load_synth_patch",  &Song::toolLoadSynthPatch },
        { "set_program",       &Song::toolSetProgram },
        { "set_automation",    &Song::toolSetAutomation },
        { "clear_automation",  &Song::toolClearAutomation },
        { "fade",              &Song::toolFade },
        { "add_bus",           &Song::toolAddBus },
        { "set_send",          &Song::toolSetSend },
        { "set_sidechain",     &Song::toolSetSidechain },
        { "sidechain_duck",    &Song::toolSidechainDuck },
        { "add_audio_clip",    &Song::toolAddAudioClip },
        { "remove_audio_clip", &Song::toolRemoveAudioClip },
        { "set_sections",      &Song::toolSetSections },
        { "copy_bars",         &Song::toolCopyBars },
        { "clear_bars",        &Song::toolClearBars },
        { "groove",            &Song::toolGroove },
        { "transpose",         &Song::toolTranspose },
        { "set_key",           &Song::toolSetKey },
        { "save_take",         &Song::toolSaveTake },
        { "use_take",          &Song::toolUseTake },
        { "delete_take",       &Song::toolDeleteTake },
        { "add_clip",          &Song::toolAddClip },
        { "import_midi",       &Song::toolImportMidi },
        { "lock",              &Song::toolLock },
        { "unlock",            &Song::toolUnlock },
        { "move_clip",         &Song::toolMoveClip },
        { "resize_clip",       &Song::toolResizeClip },
        { "delete_clip",       &Song::toolDeleteClip },
    };

    auto it = mutating.find (name);

    if (it == mutating.end())
        return fail ("Unknown tool: " + name);

    if (auto lockError = checkLocks (name, input); lockError.isNotEmpty())
        return fail (lockError);

    // A slider drag sends many set_device_params calls; keep them in one undo step.
    const auto coalesceKey = (bool) input.getProperty ("coalesce", false)
                                 ? name + input["track"].toString() + input["device"].toString() + juce::JSON::toString (input["params"], true).upToFirstOccurrenceOf (":", false, false)
                                 : juce::String();
    if (coalesceKey.isEmpty() || coalesceKey != lastCoalesceKey)
        edit->getUndoManager().beginNewTransaction (name);
    lastCoalesceKey = coalesceKey;
    lockedNotesKept = 0;
    auto result = (this->*(it->second)) (input);

    if (lockedNotesKept > 0 && result.isObject())
        result.getDynamicObject()->setProperty ("locked", juce::String (lockedNotesKept)
            + " note change(s) fell in locked bars and were not applied; locked notes are unchanged.");

    return result;
}

//==============================================================================
juce::var Song::toolAddTrack (const juce::var& input)
{
    if (getTracks().size() >= 64)
        return fail ("Track limit (64) reached");

    auto name = input.getProperty ("name", "").toString().trim();
    if (name.isEmpty())
        name = "Track " + juce::String (getTracks().size() + 1);
    auto instrument = input.getProperty ("instrument", "Synth").toString();
    const bool defaultSynth = instrument.equalsIgnoreCase ("Synth");

    juce::String error;
    auto synth = defaultSynth ? te::Plugin::Ptr() : createInstrument (instrument, error);

    if (synth == nullptr && error.isNotEmpty())
        return fail (error);

    auto track = createTrack (name, synth);

    if (track == nullptr)
        return fail ("Could not create track");

    if (defaultSynth)
    {
        useDefaultSynth (*track, "Just A Pad");
        instrument = getInstrument (*track) != nullptr ? getInstrument (*track)->getName() : "None";
    }

    return ok ({ { "track", getTracks().indexOf (track) }, { "name", name }, { "instrument", instrument } });
}

juce::var Song::toolRemoveTrack (const juce::var& input)
{
    auto track = getTrack (input);

    if (track == nullptr)
        return fail ("No such track");

    auto name = track->getName();
    edit->deleteTrack (track);
    return ok ({ { "removed", name }, { "note", "Track indices after this one shifted down by 1" } });
}

juce::var Song::toolSetTrack (const juce::var& input)
{
    auto track = getTrack (input);

    if (track == nullptr)
        return fail ("No such track");

    if (input.hasProperty ("name"))  track->setName (input["name"].toString());
    if (input.hasProperty ("mute"))  track->setMute ((bool) input["mute"]);
    if (input.hasProperty ("solo"))  track->setSolo ((bool) input["solo"]);

    if (auto vp = track->getVolumePlugin())
    {
        if (input.hasProperty ("volume_db"))
            vp->setVolumeDb (juce::jlimit (-60.0f, 6.0f, (float) (double) input["volume_db"]));

        if (input.hasProperty ("pan"))
            vp->setPan (juce::jlimit (-1.0f, 1.0f, (float) (double) input["pan"]));
    }

    return ok();
}

juce::var Song::toolAddNotes (const juce::var& input)
{
    auto track = getTrack (input);

    if (track == nullptr)
        return fail ("No such track");

    auto* notes = input["notes"].getArray();

    if (notes == nullptr)
        return fail ("notes must be an array");

    const double maxBeat = getSongBeats();
    juce::Array<juce::var> valid;
    juce::StringArray skipped;

    for (auto& n : *notes)
    {
        const int pitch = n["pitch"];
        const double start = n["start"], length = n["length"];

        if (pitch < 0 || pitch > 127 || start < 0 || start >= maxBeat || length <= 0)
            skipped.add (juce::JSON::toString (n, true));
        else
            valid.add (n);
    }

    const int clipsBefore = getMidiClips (*track).size();
    writeTrackNotes (*track, valid, (bool) input.getProperty ("replace", false));
    const int newClips = getMidiClips (*track).size() - clipsBefore;

    auto result = ok ({ { "added", valid.size() }, { "skipped", skipped.joinIntoString ("; ") } });
    if (newClips > 0)
        result.getDynamicObject()->setProperty ("clips_created", newClips);
    return result;
}

juce::var Song::toolRemoveNotes (const juce::var& input)
{
    auto track = getTrack (input);

    if (track == nullptr)
        return fail ("No such track");

    const double from = input.getProperty ("start_beat", 0.0);
    const double to = input.getProperty ("end_beat", getSongBeats());
    const int minPitch = input.getProperty ("min_pitch", 0);
    const int maxPitch = input.getProperty ("max_pitch", 127);

    juce::Array<juce::var> keep;
    int removed = 0;

    for (auto& n : readTrackNotes (*track))
    {
        if (n.start >= from && n.start < to && n.pitch >= minPitch && n.pitch <= maxPitch
             && ! isBeatLocked (*track, n.start))
            ++removed;
        else if (n.start >= from && n.start < to && n.pitch >= minPitch && n.pitch <= maxPitch)
        {
            ++lockedNotesKept;
            keep.add (makeObject ({ { "pitch", n.pitch }, { "start", n.start }, { "length", n.length }, { "velocity", n.velocity } }));
        }
        else
            keep.add (makeObject ({ { "pitch", n.pitch }, { "start", n.start }, { "length", n.length }, { "velocity", n.velocity } }));
    }

    writeTrackNotes (*track, keep, true);
    return ok ({ { "removed", removed } });
}

juce::var Song::toolSetTempo (const juce::var& input)
{
    const double bpm = input["bpm"];

    if (bpm < 40.0 || bpm > 300.0)
        return fail ("bpm must be between 40 and 300");

    auto clips = captureClipBeats();
    auto automation = captureAutomationBeats();
    edit->tempoSequence.getTempo (0)->setBpm (bpm);
    restoreClipBeats (clips);
    restoreAutomationBeats (automation);
    updateLoopRange();
    return ok ({ { "bpm", bpm } });
}

juce::var Song::toolSetSongLength (const juce::var& input)
{
    const int bars = input["bars"];

    if (bars < 1 || bars > 256)
        return fail ("bars must be between 1 and 256");

    edit->state.setProperty (barsProperty, bars, &edit->getUndoManager());
    updateLoopRange();
    return ok ({ { "bars", bars } });
}

juce::var Song::toolListInstruments (const juce::var&)
{
    juce::Array<juce::var> list;

    const bool haveSurge = findExternalPlugin ("Surge XT", true).has_value();

    for (auto& b : builtInInstruments)
        if (! (haveSurge && juce::String (b.name) == "4OSC")) // the user prefers Surge XT; 4OSC stays only for old songs
            list.add (makeObject ({ { "name", b.name }, { "kind", "built-in" }, { "description", b.description } }));

    for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
        if (d.isInstrument)
            list.add (makeObject ({ { "name", d.name }, { "kind", d.pluginFormatName },
                                    { "description", d.name.containsIgnoreCase ("Surge XT")
                                        ? juce::String ("Main synth: ~2,900 patches. Use find_synth_patches + load_synth_patch rather than set_instrument.")
                                        : d.manufacturerName + " " + d.category } }));

    return ok ({ { "instruments", list } });
}

juce::var Song::toolSetInstrument (const juce::var& input)
{
    auto track = getTrack (input);

    if (track == nullptr)
        return fail ("No such track");

    juce::String error;
    auto synth = createInstrument (input["instrument"].toString(), error);

    if (synth == nullptr && error.isNotEmpty())
        return fail (error);

    if (auto old = getInstrument (*track))
        old->deleteFromParent();

    if (synth == nullptr)
        return ok ({ { "instrument", "None" } });

    track->pluginList.insertPlugin (synth, 0, nullptr);
    return ok ({ { "instrument", synth->getName() } });
}

juce::var Song::toolListEffects (const juce::var&)
{
    juce::Array<juce::var> list;

    for (auto& b : builtInEffects)
        list.add (makeObject ({ { "name", b.name }, { "kind", "built-in" }, { "description", b.description } }));

    for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
        if (! d.isInstrument)
            list.add (makeObject ({ { "name", d.name }, { "kind", d.pluginFormatName },
                                    { "description", d.manufacturerName + " " + d.category } }));

    return ok ({ { "effects", list } });
}

juce::var Song::toolAddEffect (const juce::var& input)
{
    juce::String error;

    if ((int) input.getProperty ("track", 0) < 0)
    {
        auto fx = createEffect (input["effect"].toString(), error);

        if (fx == nullptr)
            return fail (error);

        edit->getMasterPluginList().insertPlugin (fx, edit->getMasterPluginList().size(), nullptr);
        auto result = ok ({ { "device", getMasterEffects().indexOf (fx.get()) }, { "name", fx->getName() }, { "track", "master" } });

        if (input.hasProperty ("params"))
            result.getDynamicObject()->setProperty ("params", applyParams (*fx, input["params"]));

        return result;
    }

    auto track = getTrack (input);

    if (track == nullptr)
        return fail ("No such track");

    auto fx = createEffect (input["effect"].toString(), error);

    if (fx == nullptr)
        return fail (error);

    // Effects sit after the instrument and existing effects, before the track's volume/pan.
    auto insertAt = track->pluginList.indexOf (track->getVolumePlugin());
    if (insertAt < 0)
        insertAt = track->pluginList.size();

    track->pluginList.insertPlugin (fx, insertAt, nullptr);

    auto result = ok ({ { "device", getEffects (*track).indexOf (fx.get()) }, { "name", fx->getName() } });

    if (input.hasProperty ("params"))
        result.getDynamicObject()->setProperty ("params", applyParams (*fx, input["params"]));

    return result;
}

juce::var Song::toolRemoveEffect (const juce::var& input)
{
    auto track = getTrack (input);
    const bool master = (int) input.getProperty ("track", 0) < 0;

    if (track == nullptr && ! master)
        return fail ("No such track");

    if (! (input["device"].isInt() || input["device"].isDouble() || input["device"].isInt64()))
        return fail ("device must be the effect's index (a number)");

    auto fx = master ? getMasterEffects()[(int) input["device"]] : getEffects (*track)[(int) input["device"]];

    if (fx == nullptr)
        return fail ("No effect at that index");

    auto name = fx->getName();
    fx->deleteFromParent();
    return ok ({ { "removed", name } });
}

juce::var Song::toolGetDeviceParams (const juce::var& input)
{
    auto device = findDevice ((int) input["track"], input["device"]);

    if (device == nullptr)
        return fail ("No such device. device is \"instrument\" or an effect index.");

    return ok ({ { "result", describeParams (*device, input.getProperty ("filter", "").toString(),
                                             input.getProperty ("limit", maxParamsListed)) } });
}

juce::var Song::toolSetDeviceParams (const juce::var& input)
{
    auto device = findDevice ((int) input["track"], input["device"]);

    if (device == nullptr)
        return fail ("No such device. device is \"instrument\" or an effect index.");

    return applyParams (*device, input["params"]);
}

juce::var Song::toolLoadSample (const juce::var& input)
{
    auto track = getTrack (input);

    if (track == nullptr)
        return fail ("No such track");

    const juce::File f (input["path"].toString());
    te::AudioFile audio (engine, f);

    if (! f.existsAsFile() || ! audio.isValid())
        return fail ("Not a readable audio file: " + f.getFullPathName());

    auto sampler = dynamic_cast<te::SamplerPlugin*> (getInstrument (*track));
    te::Plugin::Ptr newSampler;

    if (sampler == nullptr)
    {
        juce::String error;
        newSampler = createInstrument ("Sampler", error);
        sampler = dynamic_cast<te::SamplerPlugin*> (newSampler.get());

        if (sampler == nullptr)
            return fail ("Could not create a Sampler");
    }

    const int root = input.getProperty ("root_note", 60);
    if (root < 0 || root > 127 || (int) input.getProperty ("min_note", root) < 0 || (int) input.getProperty ("max_note", root) > 127)
        return fail ("root_note, min_note and max_note must be MIDI notes 0-127");
    const int lo = input.getProperty ("min_note", root);
    const int hi = input.getProperty ("max_note", root);

    auto error = sampler->addSound (f.getFullPathName(), f.getFileNameWithoutExtension(), 0.0, audio.getLength(), 0.0f);

    if (error.isNotEmpty())
        return fail (error);

    // Only now swap the track's instrument for the new sampler.
    if (newSampler != nullptr)
    {
        if (auto old = getInstrument (*track))
            old->deleteFromParent();

        track->pluginList.insertPlugin (newSampler, 0, nullptr);
    }

    const int index = sampler->getNumSounds() - 1;
    sampler->setSoundParams (index, root, juce::jmin (lo, hi), juce::jmax (lo, hi));

    return ok ({ { "sample", f.getFileName() }, { "root_note", root }, { "min_note", lo }, { "max_note", hi },
                 { "sounds_on_track", sampler->getNumSounds() } });
}

juce::var Song::toolLoadSfz (const juce::var& input)
{
    auto track = getTrack (input);

    if (track == nullptr)
        return fail ("No such track");

    const juce::File f (input["path"].toString());
    if (! f.existsAsFile())
        return fail ("Instrument file not found: " + f.getFullPathName());

    auto name = input.getProperty ("name", f.getFileNameWithoutExtension()).toString();
    auto inst = dynamic_cast<ClaudeSamplerPlugin*> (getInstrument (*track));
    te::Plugin::Ptr created;

    if (inst == nullptr)
    {
        juce::String error;
        created = createInstrument ("Instrument", error);
        inst = dynamic_cast<ClaudeSamplerPlugin*> (created.get());

        if (inst == nullptr)
            return fail ("Could not create the instrument player");
    }

    auto error = inst->setInstrument (f, name);
    if (error.isNotEmpty())
        return fail (error);

    // Only swap the track's instrument once the new one loaded.
    if (created != nullptr)
    {
        if (auto old = getInstrument (*track))
            old->deleteFromParent();

        track->pluginList.insertPlugin (created, 0, nullptr);
    }

    auto range = inst->getKeyRange();
    auto result = ok ({ { "instrument", name }, { "regions", inst->getNumRegions() },
                        { "range", juce::Array<juce::var> { range.getStart(), range.getEnd() } } });

    juce::Array<juce::var> ks;
    for (auto k : inst->getKeyswitches())
        ks.add (k);
    if (! ks.isEmpty())
        result.getDynamicObject()->setProperty ("keyswitches", ks);

    return result;
}

// Loads a Surge XT or OB-Xf patch (.fxp) into the track's synth, creating it if needed. Both are JUCE
// plug-ins, so their VST3 component state is exactly the data that follows the 60-byte fxp header.
juce::var Song::toolLoadSynthPatch (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    // Which synth the .fxp belongs to, from its fxp ID.
    const juce::File f (input["path"].toString());
    juce::MemoryBlock data;
    if (! f.loadFileAsData (data) || data.getSize() < 64 || memcmp (data.getData(), "CcnK", 4) != 0
        || memcmp (static_cast<const char*> (data.getData()) + 8, "FPCh", 4) != 0)
        return fail ("Not a synth patch file: " + f.getFullPathName());

    const juce::String fxId (static_cast<const char*> (data.getData()) + 16, 4);
    const juce::String synthName = fxId == "cjs3" ? "Surge XT" : fxId == "OBXf" ? "OB-Xf" : juce::String();
    if (synthName.isEmpty())
        return fail ("Unsupported patch type '" + fxId + "' in " + f.getFileName());

    auto isTarget = [synthName] (te::Plugin* p) { return dynamic_cast<te::ExternalPlugin*> (p) != nullptr && p->getName().containsIgnoreCase (synthName); };
    auto plugin = getInstrument (*track);
    te::Plugin::Ptr created;

    if (! isTarget (plugin))
    {
        juce::String error;
        created = createInstrument (synthName, error);
        if (created == nullptr || ! isTarget (created.get()))
            return fail (error.isNotEmpty() ? error : synthName + " isn't installed (scan plug-ins in the Plug-ins dialog)");
        plugin = created.get();
    }

    auto* instance = dynamic_cast<te::ExternalPlugin*> (plugin)->getAudioPluginInstance();
    if (instance == nullptr)
        return fail (synthName + " didn't load");

    juce::MemoryBlock current;
    instance->getStateInformation (current);
    auto xml = juce::AudioProcessor::getXmlFromBinary (current.getData(), (int) current.getSize());
    if (xml == nullptr || ! xml->hasTagName ("VST3PluginState"))
        xml = std::make_unique<juce::XmlElement> ("VST3PluginState");

    juce::MemoryBlock patchData (static_cast<const char*> (data.getData()) + 60, data.getSize() - 60);

    // OB-Xf patch files keep the settings on the root element, but its plug-in state expects them
    // in a <program> child ("single-program-format"); rewrap so the plug-in accepts the patch.
    if (synthName == "OB-Xf")
    {
        auto patchXml = juce::AudioProcessor::getXmlFromBinary (patchData.getData(), (int) patchData.getSize());
        if (patchXml == nullptr)
            return fail ("Couldn't read the OB-Xf patch " + f.getFileName());

        juce::XmlElement wrapped ("OB-Xf");
        wrapped.setAttribute ("ob-xf_version", patchXml->getStringAttribute ("ob-xf_version"));
        wrapped.setAttribute ("single-program-format", 1);
        auto* program = wrapped.createNewChildElement ("program");
        for (int i = 0; i < patchXml->getNumAttributes(); ++i)
            if (patchXml->getAttributeName (i) != "ob-xf_version")
                program->setAttribute (patchXml->getAttributeName (i), patchXml->getAttributeValue (i));

        patchData.reset();
        juce::AudioProcessor::copyXmlToBinary (wrapped, patchData);
    }
    if (auto* old = xml->getChildByName ("IComponent"))
        xml->removeChildElement (old, true);
    if (auto* old = xml->getChildByName ("IEditController"))
        xml->removeChildElement (old, true);
    xml->createNewChildElement ("IComponent")->addTextElement (patchData.toBase64Encoding());

    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary (*xml, state);
    instance->setStateInformation (state.getData(), (int) state.getSize());

    const auto name = input.getProperty ("name", f.getFileNameWithoutExtension()).toString();
    plugin->state.setProperty ("claudePatch", name, &edit->getUndoManager());

    if (created != nullptr)
    {
        if (auto old = getInstrument (*track))
            old->deleteFromParent();

        track->pluginList.insertPlugin (created, 0, nullptr);
    }

    return ok ({ { "instrument", synthName }, { "patch", name } });
}

// Built-in sound lists of plug-ins that have them (e.g. Dexed's DX7 voices).
juce::var Song::toolListPrograms (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    auto ext = dynamic_cast<te::ExternalPlugin*> (getInstrument (*track));
    if (ext == nullptr || ext->getNumPrograms() <= 1)
        return fail ("This track's instrument has no built-in program list");

    juce::Array<juce::var> list;
    for (int i = 0; i < ext->getNumPrograms(); ++i)
        list.add (makeObject ({ { "index", i }, { "name", ext->getProgramName (i).trim() } }));

    return ok ({ { "instrument", ext->getName() }, { "current", ext->getCurrentProgram() }, { "programs", list } });
}

juce::var Song::toolSetProgram (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    auto ext = dynamic_cast<te::ExternalPlugin*> (getInstrument (*track));
    if (ext == nullptr || ext->getNumPrograms() <= 1)
        return fail ("This track's instrument has no built-in program list");

    int index = -1;
    if (input["program"].isInt() || input["program"].isDouble())
        index = (int) input["program"];
    else
        for (int i = 0; i < ext->getNumPrograms(); ++i)
            if (ext->getProgramName (i).trim().equalsIgnoreCase (input["program"].toString().trim()))
                index = i;

    if (index < 0 || index >= ext->getNumPrograms())
        return fail ("No such program; call list_programs");

    ext->setCurrentProgram (index, true);
    const auto name = ext->getProgramName (index).trim();
    ext->state.setProperty ("claudePatch", name, &edit->getUndoManager());
    return ok ({ { "program", index }, { "name", name } });
}

//==============================================================================
te::TimePosition Song::beatToTime (double beat)
{
    return edit->tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
}

Song::AutomationTarget Song::findAutomationTarget (const juce::var& input)
{
    AutomationTarget target;
    const int trackIndex = input.getProperty ("track", -1);
    const auto device = input["device"].toString();
    const auto paramName = input["param"].toString();

    if (device == "mixer" || device.isEmpty())
    {
        te::VolumeAndPanPlugin* vp = nullptr;

        if (trackIndex < 0)
            vp = edit->getMasterVolumePlugin().get();
        else if (auto t = getTracks()[trackIndex])
            vp = t->getVolumePlugin();

        if (vp == nullptr)
            target.error = "No such track";
        else if (paramName.equalsIgnoreCase ("volume"))
            { target.param = vp->volParam.get(); target.isVolumeDb = true; }
        else if (paramName.equalsIgnoreCase ("pan"))
            target.param = vp->panParam.get();
        else
            target.error = "Mixer params are \"volume\" (dB) and \"pan\" (-1..1)";

        return target;
    }

    auto plugin = findDevice (trackIndex, input["device"]);

    if (plugin == nullptr)
    {
        target.error = "No such device. device is \"mixer\", \"instrument\" or an effect index.";
        return target;
    }

    for (auto param : plugin->getAutomatableParameters())
        if (param->paramID.equalsIgnoreCase (paramName) || param->getParameterName().equalsIgnoreCase (paramName))
            target.param = param;

    if (target.param == nullptr)
        target.error = "No param '" + paramName + "' on " + plugin->getName() + ". Call get_device_params.";

    return target;
}

juce::var Song::toolSetAutomation (const juce::var& input)
{
    auto target = findAutomationTarget (input);

    if (target.param == nullptr)
        return fail (target.error);

    auto* points = input["points"].getArray();

    if (points == nullptr || points->isEmpty())
        return fail ("points must be a non-empty array of {beat, value, curve?}");

    auto* um = &edit->getUndoManager();
    auto& curve = target.param->getCurve();
    const auto range = target.param->getValueRange();

    double first = 1.0e9, last = -1.0;
    for (auto& p : *points)
    {
        first = juce::jmin (first, (double) p["beat"]);
        last = juce::jmax (last, (double) p["beat"]);
    }

    // New points replace any existing automation in the span they cover.
    curve.removePointsInRegion (te::TimeRange (beatToTime (first), beatToTime (last) + 0.001s), um);

    for (auto& p : *points)
    {
        float value = (float) (double) p["value"];
        value = target.isVolumeDb ? te::decibelsToVolumeFaderPosition (juce::jlimit (-100.0f, 6.0f, value))
                                  : juce::jlimit (range.getStart(), range.getEnd(), value);

        curve.addPoint (beatToTime (juce::jlimit (0.0, getSongBeats(), (double) p["beat"])), value,
                        juce::jlimit (-1.0f, 1.0f, (float) (double) p.getProperty ("curve", 0.0)), um);
    }

    return ok ({ { "param", target.param->getParameterName() }, { "points_on_curve", curve.getNumPoints() } });
}

juce::var Song::toolClearAutomation (const juce::var& input)
{
    auto target = findAutomationTarget (input);

    if (target.param == nullptr)
        return fail (target.error);

    auto* um = &edit->getUndoManager();
    auto& curve = target.param->getCurve();

    if (input.hasProperty ("start_beat") || input.hasProperty ("end_beat"))
        curve.removePointsInRegion (te::TimeRange (beatToTime (input.getProperty ("start_beat", 0.0)),
                                                   beatToTime (input.getProperty ("end_beat", getSongBeats()))), um);
    else
        curve.clear (um);

    return ok ({ { "points_on_curve", curve.getNumPoints() } });
}

juce::var Song::toolFade (const juce::var& input)
{
    const auto type = input["type"].toString().toLowerCase();
    if (type != "in" && type != "out")
        return fail ("type must be \"in\" or \"out\"");

    const bool fadeIn = type == "in";
    const double start = input["start_beat"], end = input["end_beat"];

    if (end <= start)
        return fail ("end_beat must be after start_beat");

    auto vpInput = makeObject ({ { "track", input.getProperty ("track", -1) }, { "device", "mixer" }, { "param", "volume" } });
    auto target = findAutomationTarget (vpInput);

    if (target.param == nullptr)
        return fail (target.error);

    // Fade between silence and the fader's current level.
    const float levelDb = te::volumeFaderPositionToDB (target.param->getCurrentValue());
    const float shape = input["shape"].toString() == "linear" ? 0.0f : (fadeIn ? 0.5f : -0.5f);

    juce::Array<juce::var> points;
    points.add (makeObject ({ { "beat", start }, { "value", fadeIn ? -100.0f : levelDb }, { "curve", shape } }));
    points.add (makeObject ({ { "beat", end },   { "value", fadeIn ? levelDb : -100.0f } }));

    if (fadeIn && start > 0)
        points.insert (0, makeObject ({ { "beat", 0.0 }, { "value", -100.0f } }));
    if (! fadeIn && end < getSongBeats())
        points.add (makeObject ({ { "beat", getSongBeats() }, { "value", -100.0f } }));

    vpInput.getDynamicObject()->setProperty ("points", points);
    return toolSetAutomation (vpInput);
}

juce::var Song::describeAutomation (const juce::Array<te::AutomatableParameter*>& params)
{
    juce::Array<juce::var> lanes;

    for (auto param : params)
    {
        auto& curve = param->getCurve();

        if (curve.getNumPoints() == 0)
            continue;

        const bool isVolume = dynamic_cast<te::VolumeAndPanPlugin*> (param->getPlugin()) != nullptr
                               && param->paramID.containsIgnoreCase ("volume");
        const auto range = param->getValueRange();
        juce::Array<juce::var> points;

        for (int i = 0; i < curve.getNumPoints(); ++i)
        {
            const auto beat = edit->tempoSequence.toBeats (te::toTime (curve.getPointPosition (i), edit->tempoSequence)).inBeats();
            const float v = curve.getPointValue (i);
            points.add (makeObject ({ { "beat", beat },
                                      { "value", isVolume ? te::volumeFaderPositionToDB (v) : v },
                                      { "norm", range.getLength() > 0 ? (v - range.getStart()) / range.getLength() : 0.0f },
                                      { "curve", curve.getPointCurve (i) } }));
        }

        lanes.add (makeObject ({ { "device", param->getPlugin() != nullptr ? param->getPlugin()->getName() : juce::String() },
                                 { "param", param->getParameterName() },
                                 { "points", points } }));
    }

    return lanes;
}
