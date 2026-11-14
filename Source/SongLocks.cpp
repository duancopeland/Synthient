// Locks: a whole track or a range of bars on a track can be locked. Locked notes are
// never changed by any tool (writeTrackNotes keeps them), and a fully locked track
// also refuses sound and clip changes. Mixer settings stay editable.
#include "Song.h"

namespace
{
    const juce::Identifier locksProperty ("claudeDawLocks");
    constexpr double wholeTrackEnd = 1.0e9;

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
}

juce::Array<juce::var> Song::readLocks()
{
    auto v = juce::JSON::parse (edit->state.getProperty (locksProperty).toString());
    return v.isArray() ? *v.getArray() : juce::Array<juce::var>();
}

void Song::writeLocks (const juce::Array<juce::var>& locks)
{
    edit->state.setProperty (locksProperty, juce::JSON::toString (locks, true), &edit->getUndoManager());
}

std::vector<std::pair<double, double>> Song::getLockedRanges (te::AudioTrack& track)
{
    std::vector<std::pair<double, double>> ranges;
    const auto id = track.itemID.toString();

    for (auto& l : readLocks())
        if (l["track"].toString() == id)
            ranges.push_back ({ (double) l["start_beat"], (double) l["end_beat"] });

    return ranges;
}

bool Song::isTrackLocked (te::AudioTrack& track)
{
    for (auto& [from, to] : getLockedRanges (track))
        if (from <= 0.0 && to >= wholeTrackEnd)
            return true;

    return false;
}

bool Song::isBeatLocked (te::AudioTrack& track, double beat)
{
    for (auto& [from, to] : getLockedRanges (track))
        if (beat >= from - 1.0e-6 && beat < to - 1.0e-6)
            return true;

    return false;
}

bool Song::rangeTouchesLock (te::AudioTrack& track, double from, double to)
{
    for (auto& [a, b] : getLockedRanges (track))
        if (from < b && to > a)
            return true;

    return false;
}

juce::var Song::describeLocks (te::AudioTrack& track)
{
    juce::Array<juce::var> ranges;

    for (auto& [from, to] : getLockedRanges (track))
        if (to < wholeTrackEnd)
            ranges.add (makeObject ({ { "start_beat", from }, { "end_beat", to } }));

    return makeObject ({ { "locked", isTrackLocked (track) }, { "ranges", ranges } });
}

juce::var Song::toolLock (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    double from = 0.0, to = wholeTrackEnd;
    if (input.hasProperty ("start_bar") || input.hasProperty ("end_bar"))
    {
        from = ((int) input.getProperty ("start_bar", 1) - 1) * (double) beatsPerBar;
        to = ((int) input.getProperty ("end_bar", getBars() + 1) - 1) * (double) beatsPerBar;
        if (to <= from)
            return fail ("end_bar must be after start_bar (exclusive end)");
    }

    auto locks = readLocks();
    locks.add (makeObject ({ { "track", track->itemID.toString() }, { "start_beat", from }, { "end_beat", to } }));
    writeLocks (locks);
    return ok ({ { "locked", to >= wholeTrackEnd ? juce::String ("whole track")
                                                 : "bars " + juce::String ((int) (from / beatsPerBar) + 1) + "-" + juce::String ((int) (to / beatsPerBar)) } });
}

juce::var Song::toolUnlock (const juce::var& input)
{
    auto track = getTrack (input);
    if (track == nullptr)
        return fail ("No such track");

    const auto id = track->itemID.toString();
    const bool ranged = input.hasProperty ("start_bar") || input.hasProperty ("end_bar");
    const double from = ((int) input.getProperty ("start_bar", 1) - 1) * (double) beatsPerBar;
    const double to = ((int) input.getProperty ("end_bar", 100000) - 1) * (double) beatsPerBar;

    juce::Array<juce::var> kept;
    int removed = 0;

    for (auto& l : readLocks())
    {
        const bool mine = l["track"].toString() == id;
        const bool overlaps = (double) l["start_beat"] < to && (double) l["end_beat"] > from;

        if (mine && (! ranged || overlaps))
            ++removed;
        else
            kept.add (l);
    }

    writeLocks (kept);
    return ok ({ { "removed_locks", removed } });
}

juce::String Song::checkLocks (const juce::String& tool, const juce::var& input)
{
    // Tools that change a track's sound, clips or structure are refused on a fully locked track.
    static const juce::StringArray soundAndStructure {
        "remove_track", "set_instrument", "add_effect", "remove_effect", "set_device_params", "load_sample", "load_sfz", "load_synth_patch", "set_program",
        "add_audio_clip", "remove_audio_clip", "set_sidechain", "sidechain_duck", "use_take",
        "add_clip", "move_clip", "resize_clip", "delete_clip", "set_automation", "clear_automation", "fade"
    };

    if (! soundAndStructure.contains (tool) || ! input.hasProperty ("track"))
        return {};

    auto track = getTracks()[(int) input["track"]];
    if (track == nullptr)
        return {};

    // Mixer automation (volume/pan) stays editable on locked tracks.
    if ((tool == "set_automation" || tool == "clear_automation" || tool == "fade")
         && (input["device"].toString() == "mixer" || tool == "fade"))
        return {};

    if (isTrackLocked (*track))
        return "Track \"" + track->getName() + "\" is locked. Ask the user to unlock it (or use unlock) first.";

    if (tool == "remove_track" && ! getLockedRanges (*track).empty())
        return "Track \"" + track->getName() + "\" has locked bars. Unlock them before deleting the track.";

    // Clip moves/resizes/deletes may not disturb locked bars.
    if (tool == "move_clip" || tool == "resize_clip" || tool == "delete_clip")
    {
        if (auto clip = getMidiClips (*track)[(int) input["clip"]])
        {
            const double from = clip->getStartBeat().inBeats(), to = clip->getEndBeat().inBeats();
            if (rangeTouchesLock (*track, from, to))
                return "That clip contains locked bars on \"" + track->getName() + "\".";

            if (tool == "resize_clip")
            {
                const double newFrom = input.hasProperty ("start_bar") ? ((int) input["start_bar"] - 1) * (double) beatsPerBar : from;
                const double newTo = input.hasProperty ("end_bar") ? ((int) input["end_bar"] - 1) * (double) beatsPerBar : to;
                if (rangeTouchesLock (*track, newFrom, newTo))
                    return "The new clip range contains locked bars.";
            }

            if (tool == "move_clip")
            {
                const double dest = ((int) input["dest_bar"] - 1) * (double) beatsPerBar;
                if (rangeTouchesLock (*track, dest, dest + (to - from)))
                    return "The destination contains locked bars.";
            }
        }
    }

    return {};
}
