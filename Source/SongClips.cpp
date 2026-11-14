// MIDI clips: a track holds any number of non-overlapping MIDI clips. Notes are
// exposed in absolute song beats and routed to the clip that covers them.
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

    double startBeat (te::Clip& c)   { return c.getStartBeat().inBeats(); }
    double endBeat (te::Clip& c)     { return c.getEndBeat().inBeats(); }
}

//==============================================================================
juce::Array<te::MidiClip*> Song::getMidiClips (te::AudioTrack& track)
{
    juce::Array<te::MidiClip*> clips;

    for (auto c : track.getClips())
        if (auto mc = dynamic_cast<te::MidiClip*> (c))
            clips.add (mc);

    std::sort (clips.begin(), clips.end(), [] (auto a, auto b) { return startBeat (*a) < startBeat (*b); });
    return clips;
}

te::MidiClip* Song::createMidiClip (te::AudioTrack& track, double fromBeat, double toBeat, const juce::String& name)
{
    auto clip = track.insertMIDIClip (name.isNotEmpty() ? name : track.getName(),
                                      { beatToTime (fromBeat), beatToTime (toBeat) }, nullptr);
    return clip.get();
}

std::vector<Song::NoteData> Song::readTrackNotes (te::AudioTrack& track)
{
    std::vector<NoteData> notes;

    for (auto clip : getMidiClips (track))
    {
        const double contentStart = clip->getContentStartBeat().inBeats();
        const double from = startBeat (*clip), to = endBeat (*clip);

        for (auto n : clip->getSequence().getNotes())
        {
            const double s = contentStart + n->getStartBeat().inBeats();

            // Notes trimmed out of view by the clip's edges don't play, so don't report them.
            if (s < from - 1.0e-6 || s >= to - 1.0e-6)
                continue;

            notes.push_back ({ n->getNoteNumber(), s, juce::jmin (n->getLengthBeats().inBeats(), to - s), n->getVelocity() });
        }
    }

    std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    return notes;
}

void Song::writeTrackNotes (te::AudioTrack& track, const juce::Array<juce::var>& notes, bool clearFirst)
{
    auto* um = &edit->getUndoManager();
    const double songEnd = getSongBeats();
    const bool hasLocks = ! getLockedRanges (track).empty();

    // Notes inside locked bars survive a rewrite unchanged.
    juce::Array<juce::var> lockedNotes;
    if (clearFirst && hasLocks)
        for (auto& n : readTrackNotes (track))
            if (isBeatLocked (track, n.start))
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("pitch", n.pitch);
                o->setProperty ("start", n.start);
                o->setProperty ("length", n.length);
                o->setProperty ("velocity", n.velocity);
                lockedNotes.add (juce::var (o));
            }

    if (clearFirst)
    {
        for (auto clip : getMidiClips (track))
        {
            const double contentStart = clip->getContentStartBeat().inBeats();
            const double from = startBeat (*clip), to = juce::jmin (endBeat (*clip), songEnd);
            auto& seq = clip->getSequence();
            juce::Array<te::MidiNote*> visible;

            for (auto n : seq.getNotes())
            {
                const double s = contentStart + n->getStartBeat().inBeats();
                if (s >= from - 1.0e-6 && s < to - 1.0e-6)
                    visible.add (n);
            }

            for (auto n : visible)
                seq.removeNote (*n, um);
        }
    }

    juce::Array<juce::var> toWrite;
    for (auto& n : notes)
    {
        if (hasLocks && isBeatLocked (track, (double) n["start"]))
        {
            // Unchanged locked notes come back through lockedNotes; anything else is refused.
            bool same = false;
            for (auto& l : lockedNotes)
                if ((int) l["pitch"] == (int) n["pitch"] && std::abs ((double) l["start"] - (double) n["start"]) < 1.0e-6
                     && std::abs ((double) l["length"] - (double) n["length"]) < 1.0e-6)
                    same = true;

            if (! same)
                ++lockedNotesKept;
            continue;
        }
        toWrite.add (n);
    }
    toWrite.addArray (lockedNotes);

    for (auto& n : toWrite)
    {
        const int pitch = n["pitch"];
        const double s = n["start"], length = n["length"];

        if (pitch < 0 || pitch > 127 || s < 0 || s >= songEnd || length <= 0)
            continue;

        te::MidiClip* target = nullptr;
        auto clips = getMidiClips (track);

        for (auto c : clips)
            if (s >= startBeat (*c) - 1.0e-6 && s < endBeat (*c) - 1.0e-6)
                target = c;

        if (target == nullptr)
        {
            // Fill the gap around the note, from its bar up to the next clip (or the song end).
            double from = std::floor (s / beatsPerBar) * beatsPerBar, to = songEnd;

            for (auto c : clips)
            {
                if (endBeat (*c) <= s + 1.0e-6)  from = juce::jmax (from, endBeat (*c));
                if (startBeat (*c) > s)          to = juce::jmin (to, startBeat (*c));
            }

            target = createMidiClip (track, from, to, {});
            if (target == nullptr)
                continue;
        }

        const double contentStart = target->getContentStartBeat().inBeats();
        target->getSequence().addNote (pitch, te::BeatPosition::fromBeats (s - contentStart),
                                       te::BeatDuration::fromBeats (juce::jmin (length, endBeat (*target) - s)),
                                       juce::jlimit (1, 127, (int) n.getProperty ("velocity", 100)), 0, um);
    }
}

juce::var Song::describeMidiClips (te::AudioTrack& track)
{
    juce::Array<juce::var> list;
    int index = 0;

    for (auto c : getMidiClips (track))
        list.add (makeObject ({ { "clip", index++ }, { "name", c->getName() },
                                { "start_beat", startBeat (*c) }, { "end_beat", endBeat (*c) } }));

    return list;
}

//==============================================================================
// Keep every clip at the same bars when the tempo changes (clips are positioned in seconds).

std::vector<Song::ClipBeats> Song::captureClipBeats()
{
    std::vector<ClipBeats> result;

    for (auto t : getTracks())
        for (auto c : t->getClips())
            result.push_back ({ c, startBeat (*c), endBeat (*c), c->getOffsetInBeats().inBeats() });

    return result;
}

void Song::restoreClipBeats (const std::vector<ClipBeats>& saved)
{
    for (auto& s : saved)
    {
        const auto start = beatToTime (s.start), end = beatToTime (s.end);
        const auto offset = beatToTime (s.start + s.offset) - start;
        s.clip->setPosition (te::ClipPosition { { start, end }, offset });
    }
}

std::vector<Song::AutomationBeats> Song::captureAutomationBeats()
{
    juce::Array<te::AutomatableParameter*> params;

    for (auto t : getTracks())
        params.addArray (t->getAllAutomatableParams());

    params.addArray (edit->getMasterVolumePlugin()->getAutomatableParameters());
    for (auto p : edit->getMasterPluginList().getPlugins())
        params.addArray (p->getAutomatableParameters());

    std::vector<AutomationBeats> result;

    for (auto param : params)
    {
        auto& curve = param->getCurve();
        if (curve.getNumPoints() == 0)
            continue;

        AutomationBeats a { param, {} };
        for (int i = 0; i < curve.getNumPoints(); ++i)
            a.points.emplace_back (edit->tempoSequence.toBeats (te::toTime (curve.getPointPosition (i), edit->tempoSequence)).inBeats(),
                                   curve.getPointValue (i), curve.getPointCurve (i));
        result.push_back (std::move (a));
    }

    return result;
}

void Song::restoreAutomationBeats (const std::vector<AutomationBeats>& saved)
{
    auto* um = &edit->getUndoManager();

    for (auto& a : saved)
    {
        auto& curve = a.param->getCurve();
        curve.clear (um);
        for (auto& [beat, value, shape] : a.points)
            curve.addPoint (beatToTime (beat), value, shape, um);
    }
}

//==============================================================================
static bool overlapsOther (juce::Array<te::MidiClip*> clips, te::MidiClip* self, double from, double to)
{
    for (auto c : clips)
        if (c != self && from < endBeat (*c) - 1.0e-6 && to > startBeat (*c) + 1.0e-6)
            return true;

    return false;
}

juce::var Song::toolAddClip (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    const int fromBar = input["start_bar"], toBar = input["end_bar"];
    if (fromBar < 1 || toBar <= fromBar)
        return fail ("Need start_bar >= 1 and end_bar > start_bar (exclusive end)");

    const double from = (fromBar - 1) * (double) beatsPerBar, to = (toBar - 1) * (double) beatsPerBar;
    if (to > getSongBeats())
        return fail ("Clip runs past the song end; call set_song_length first");

    if (overlapsOther (getMidiClips (*track), nullptr, from, to))
        return fail ("That range overlaps an existing clip; move or delete it first");

    auto clip = createMidiClip (*track, from, to, input.getProperty ("name", "").toString());
    return ok ({ { "clip", getMidiClips (*track).indexOf (clip) } });
}

juce::var Song::toolMoveClip (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    auto clips = getMidiClips (*track);
    auto clip = clips[(int) input["clip"]];
    if (clip == nullptr)
        return fail ("No clip at that index (see clips in get_song)");

    const double length = endBeat (*clip) - startBeat (*clip);
    const double dest = ((int) input["dest_bar"] - 1) * (double) beatsPerBar;
    const bool copy = input.getProperty ("copy", false);

    if (dest < 0 || dest + length > getSongBeats() + 1.0e-6)
        return fail ("Destination is outside the song; extend it with set_song_length first");

    if (overlapsOther (clips, copy ? nullptr : clip, dest, dest + length))
        return fail ("Destination overlaps another clip; delete or move that clip first");

    te::MidiClip* target = clip;

    if (copy)
    {
        auto dup = te::duplicateClip (*clip);
        target = dynamic_cast<te::MidiClip*> (dup.get());
        if (target == nullptr)
            return fail ("Could not duplicate clip");
    }

    const auto offset = target->getPosition().offset;
    target->setPosition (te::ClipPosition { { beatToTime (dest), beatToTime (dest + length) }, offset });
    return ok ({ { "clip", getMidiClips (*track).indexOf (target) }, { "start_bar", (int) (dest / beatsPerBar) + 1 } });
}

juce::var Song::toolResizeClip (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    auto clips = getMidiClips (*track);
    auto clip = clips[(int) input["clip"]];
    if (clip == nullptr)
        return fail ("No clip at that index");

    const double oldStart = startBeat (*clip), oldOffset = clip->getOffsetInBeats().inBeats();
    double from = input.hasProperty ("start_bar") ? ((int) input["start_bar"] - 1) * (double) beatsPerBar : oldStart;
    double to = input.hasProperty ("end_bar") ? ((int) input["end_bar"] - 1) * (double) beatsPerBar : endBeat (*clip);

    if (from < 0 || to <= from || to > getSongBeats() + 1.0e-6)
        return fail ("Invalid range");
    if (overlapsOther (clips, clip, from, to))
        return fail ("New range overlaps another clip");

    // Keep notes where they are in the song: shift the offset with the start. Extending left
    // past the content start would need a negative offset, so move the notes later instead.
    double newOffset = oldOffset + (from - oldStart);
    if (newOffset < 0)
    {
        const auto shift = te::BeatDuration::fromBeats (-newOffset);
        for (auto n : clip->getSequence().getNotes())
            n->setStartAndLength (n->getStartBeat() + shift, n->getLengthBeats(), &edit->getUndoManager());
        newOffset = 0;
    }
    const auto start = beatToTime (from);
    clip->setPosition (te::ClipPosition { { start, beatToTime (to) }, beatToTime (from + newOffset) - start });
    return ok();
}

juce::var Song::toolDeleteClip (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    auto clip = getMidiClips (*track)[(int) input["clip"]];
    if (clip == nullptr)
        return fail ("No clip at that index");

    clip->removeFromParent();
    return ok();
}
