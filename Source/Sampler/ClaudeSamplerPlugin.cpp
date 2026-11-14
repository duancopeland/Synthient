#include "ClaudeSamplerPlugin.h"

namespace
{
    const juce::Identifier sfzId ("sfz"), nameId ("instrumentName");
    constexpr float silence = 1.0e-5f;   // -100 dB: envelopes end here
    constexpr float fastRelease = 0.006f;

    juce::String dbText (float v)       { return juce::String (v, 1) + " dB"; }
    juce::String secondsText (float v)  { return juce::String (v, 2) + " s"; }

    float coefficientFor (float seconds, double sampleRate) // falls 100 dB over `seconds`
    {
        return (float) std::exp (std::log (silence) / juce::jmax (1.0, seconds * sampleRate));
    }
}

const char* ClaudeSamplerPlugin::xmlTypeName = "claudesampler";

ClaudeSamplerPlugin::ClaudeSamplerPlugin (te::PluginCreationInfo info) : Plugin (info)
{
    auto um = getUndoManager();

    sfzPath.referTo (state, sfzId, um);
    displayName.referTo (state, nameId, um);
    volumeValue.referTo (state, "level", um, 0.0f);
    panValue.referTo (state, "balance", um, 0.0f);
    tuneValue.referTo (state, "tune", um, 0.0f);
    attackValue.referTo (state, "attack", um, 0.0f);
    releaseValue.referTo (state, "release", um, 1.0f);
    velSensValue.referTo (state, "velSens", um, 1.0f);

    auto number = [] (const juce::String& s) { return s.getFloatValue(); };

    volume = addParam ("level", "Volume", { -40.0f, 12.0f }, dbText, number);
    pan = addParam ("balance", "Pan", { -1.0f, 1.0f }, [] (float v) { return juce::String (v, 2); }, number);
    tune = addParam ("tune", "Tune (semitones)", { -24.0f, 24.0f }, [] (float v) { return juce::String (v, 2) + " st"; }, number);
    attack = addParam ("attack", "Extra attack", { 0.0f, 4.0f }, secondsText, number);
    release = addParam ("release", "Release length", { 0.1f, 4.0f }, [] (float v) { return juce::String (v, 2) + "x"; }, number);
    velocitySensitivity = addParam ("velSens", "Velocity sensitivity", { 0.0f, 1.0f }, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }, number);

    volume->attachToCurrentValue (volumeValue);
    pan->attachToCurrentValue (panValue);
    tune->attachToCurrentValue (tuneValue);
    attack->attachToCurrentValue (attackValue);
    release->attachToCurrentValue (releaseValue);
    velocitySensitivity->attachToCurrentValue (velSensValue);

    reloadFromState();
}

ClaudeSamplerPlugin::~ClaudeSamplerPlugin()
{
    notifyListenersOfDeletion();

    for (auto p : { volume, pan, tune, attack, release, velocitySensitivity })
        p->detachFromCurrentValue();
}

//==============================================================================
juce::String ClaudeSamplerPlugin::setInstrument (const juce::File& file, const juce::String& name)
{
    juce::String error;
    auto inst = sfz::loadInstrument (file, error);

    if (inst == nullptr)
        return error;

    preloaded = inst;

    auto root = libraryRoot ? libraryRoot() : juce::File();
    auto stored = root != juce::File() && file.isAChildOf (root)
                    ? file.getRelativePathFrom (root).replaceCharacter ('\\', '/')
                    : file.getFullPathName();

    state.setProperty (nameId, name, getUndoManager());

    if (state[sfzId].toString() == stored)
        reloadFromState(); // same file: still reset the voices and pick up edits to it
    else
        state.setProperty (sfzId, stored, getUndoManager());

    return loadError;
}

void ClaudeSamplerPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
{
    if (v == state && id == sfzId)
        reloadFromState();

    Plugin::valueTreePropertyChanged (v, id);
}

void ClaudeSamplerPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    te::copyValueTree (state, v, getUndoManager());
    reloadFromState();
}

void ClaudeSamplerPlugin::reloadFromState()
{
    auto loaded = std::make_shared<Loaded>();
    auto path = state[sfzId].toString(); // not sfzPath.get(): the CachedValue may not have updated yet
    loadError = {};

    if (path.isNotEmpty())
    {
        auto file = juce::File::isAbsolutePath (path) ? juce::File (path)
                                                      : (libraryRoot ? libraryRoot() : juce::File()).getChildFile (path);

        if (preloaded != nullptr && preloaded->file == file)
            loaded->instrument = preloaded;
        else
            loaded->instrument = sfz::loadInstrument (file, loadError);

        preloaded = nullptr;

        if (auto inst = loaded->instrument)
        {
            loaded->seqCounters.assign (inst->regions.size(), 0);
            juce::Thread::launch ([inst] { inst->touchSamples(); });
        }
    }

    keepAlive.push_back (loaded);

    const juce::SpinLock::ScopedLockType sl (pendingLock);
    pending = loaded;
    hasPending = true;
}

int ClaudeSamplerPlugin::getNumRegions() const
{
    auto inst = keepAlive.empty() ? nullptr : keepAlive.back()->instrument;
    return inst != nullptr ? (int) inst->regions.size() : 0;
}

juce::Range<int> ClaudeSamplerPlugin::getKeyRange() const
{
    auto inst = keepAlive.empty() ? nullptr : keepAlive.back()->instrument;
    return inst != nullptr ? juce::Range<int> (inst->lowestKey, inst->highestKey) : juce::Range<int>();
}

juce::Array<int> ClaudeSamplerPlugin::getKeyswitches() const
{
    juce::Array<int> result;
    if (auto inst = keepAlive.empty() ? nullptr : keepAlive.back()->instrument; inst != nullptr && inst->hasKeyswitches())
        for (int k = inst->swLokey; k <= inst->swHikey; ++k)
            result.add (k);
    return result;
}

//==============================================================================
void ClaudeSamplerPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    killAll = true;
}

void ClaudeSamplerPlugin::deinitialise() {}
void ClaudeSamplerPlugin::reset()       { killAll = true; }
void ClaudeSamplerPlugin::midiPanic()   { killAll = true; }

void ClaudeSamplerPlugin::takePending()
{
    const juce::SpinLock::ScopedTryLockType sl (pendingLock);

    if (! sl.isLocked() || ! hasPending)
        return;

    active = pending; // the message thread keeps every Loaded alive, so nothing is freed here
    hasPending = false;

    for (auto& v : voices)
        v.active = false;

    cc = {};
    if (active->instrument != nullptr)
    {
        cc = active->instrument->ccDefaults;
        currentKeyswitch = active->instrument->swDefault;
    }

    keyDown = {};
    pendingRelease = {};
    keysHeld = 0;
    pedal = false;
}

void ClaudeSamplerPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    takePending();

    auto& buffer = *fc.destBuffer;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, fc.bufferStartSample, fc.bufferNumSamples);

    if (killAll)
    {
        killAll = false;
        for (auto& v : voices) v.active = false;
        keyDown = {};
        pendingRelease = {};
        keysHeld = 0;
        pedal = false;
    }

    if (active == nullptr || active->instrument == nullptr || buffer.getNumChannels() == 0)
        return;

    blockPos = 0;

    if (auto* midi = fc.bufferForMidiMessages)
    {
        if (midi->isAllNotesOff)
        {
            for (auto& v : voices)
                if (v.active) releaseVoice (v, true);
            keyDown = {};
            pendingRelease = {};
            keysHeld = 0;
            pedal = false;
        }

        for (auto& m : *midi)
        {
            const int at = juce::jlimit (0, fc.bufferNumSamples, juce::roundToInt (m.getTimeStamp() * sampleRate));

            if (at > blockPos)
            {
                renderVoices (buffer, fc.bufferStartSample + blockPos, at - blockPos);
                blockPos = at;
            }

            if (m.isNoteOn())
                noteOn (m.getNoteNumber(), m.getVelocity(), 0);
            else if (m.isNoteOff())
                noteOff (m.getNoteNumber(), 0);
            else if (m.isSustainPedalOn())
                pedal = true;
            else if (m.isSustainPedalOff())
            {
                pedal = false;
                for (int n = 0; n < 128; ++n)
                    if (pendingRelease[(size_t) n] && ! keyDown[(size_t) n])
                        releaseKey (n, 0);
            }
            else if (m.isController())
                cc[(size_t) m.getControllerNumber()] = m.getControllerValue() / 127.0f;
            else if (m.isPitchWheel())
                pitchBend = juce::jlimit (-1.0f, 1.0f, (m.getPitchWheelValue() - 8192) / 8192.0f);
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                for (auto& v : voices)
                    if (v.active) releaseVoice (v, true);
        }
    }

    if (blockPos < fc.bufferNumSamples)
        renderVoices (buffer, fc.bufferStartSample + blockPos, fc.bufferNumSamples - blockPos);

    sampleClock += fc.bufferNumSamples;
}

//==============================================================================
bool ClaudeSamplerPlugin::regionMatches (const sfz::Region& r, int index, int note, int velocity, float rand)
{
    if (note < r.lokey || note > r.hikey || velocity < r.lovel || velocity > r.hivel)
        return false;

    if (r.swLast >= 0 && r.swLast != currentKeyswitch)
        return false;

    for (auto& range : r.ccRanges)
    {
        const float value = cc[(size_t) range.cc] * 127.0f;
        if (value < range.lo - 0.01f || value > range.hi + 0.01f)
            return false;
    }

    if (r.seqLength > 1)
    {
        auto& counter = active->seqCounters[(size_t) index];
        const int position = counter++ % r.seqLength;
        if (position != r.seqPosition - 1)
            return false;
    }

    return rand >= r.lorand && (rand < r.hirand || (r.hirand >= 1.0f && rand <= 1.0f));
}

void ClaudeSamplerPlugin::noteOn (int note, int velocity, int)
{
    auto& inst = *active->instrument;

    if (inst.hasKeyswitches() && note >= inst.swLokey && note <= inst.swHikey)
    {
        currentKeyswitch = note;
        return;
    }

    if (! keyDown[(size_t) note])
        ++keysHeld;

    keyDown[(size_t) note] = true;
    keyVelocity[(size_t) note] = velocity;
    keyDownAt[(size_t) note] = sampleClock + blockPos;

    // sfizz's extended CCs: velocity, note, random, alternate
    cc[131] = velocity / 127.0f;
    cc[133] = note / 127.0f;
    cc[135] = random.nextFloat();
    cc[136] = random.nextFloat() * 2.0f - 1.0f;
    alternate = ! alternate;
    cc[137] = alternate ? 1.0f : 0.0f;

    const float rand = random.nextFloat();
    const auto eventId = ++eventCounter;

    for (size_t i = 0; i < inst.regions.size(); ++i)
    {
        auto& r = inst.regions[i];

        if (r.trigger == sfz::Trigger::release
            || (r.trigger == sfz::Trigger::first && keysHeld != 1)
            || (r.trigger == sfz::Trigger::legato && keysHeld < 2))
            continue;

        if (regionMatches (r, (int) i, note, velocity, rand))
            startVoice (r, note, velocity, 0, 0.0f, eventId);
    }
}

void ClaudeSamplerPlugin::noteOff (int note, int offset)
{
    auto& inst = *active->instrument;

    if (inst.hasKeyswitches() && note >= inst.swLokey && note <= inst.swHikey)
        return;

    if (! keyDown[(size_t) note])
        return;

    keyDown[(size_t) note] = false;
    --keysHeld;

    if (pedal)
        pendingRelease[(size_t) note] = true;
    else
        releaseKey (note, offset);
}

void ClaudeSamplerPlugin::releaseKey (int note, int)
{
    pendingRelease[(size_t) note] = false;

    for (auto& v : voices)
        if (v.active && v.note == note && ! v.released && ! v.releaseVoice && v.region->loopMode != sfz::LoopMode::oneShot)
            releaseVoice (v, false);

    // release samples (piano dampers, guitar fret noise), quieter the longer the key was held
    auto& inst = *active->instrument;
    const float held = (float) ((sampleClock + blockPos - keyDownAt[(size_t) note]) / sampleRate);
    const float rand = random.nextFloat();
    const auto eventId = ++eventCounter;

    for (size_t i = 0; i < inst.regions.size(); ++i)
    {
        auto& r = inst.regions[i];
        if (r.trigger == sfz::Trigger::release && regionMatches (r, (int) i, note, keyVelocity[(size_t) note], rand))
            startVoice (r, note, keyVelocity[(size_t) note], 0, -r.rtDecay * held, eventId);
    }
}

void ClaudeSamplerPlugin::startVoice (const sfz::Region& r, int note, int velocity, int offset, float extraDb, juce::uint64 eventId)
{
    // choke groups (e.g. a closed hi-hat cuts the open one)
    for (auto& v : voices)
        if (v.active && v.eventId != eventId && v.region->hasOffBy && v.region->offBy == r.group)
            releaseVoice (v, v.region->offModeFast);

    Voice* voice = nullptr;
    for (auto& v : voices)
        if (! v.active) { voice = &v; break; }

    if (voice == nullptr) // steal: the oldest released voice, else the oldest
        for (auto& v : voices)
            if (voice == nullptr || (v.released && ! voice->released) || (v.released == voice->released && v.age < voice->age))
                voice = &v;

    float amplitude = r.amplitude, vol = r.volume + extraDb, panPos = r.pan, tuneCents = r.tune;
    float startOffset = (float) r.offset, delay = r.delay;
    float ampDelay = r.ampDelay, att = r.attack, hold = r.hold, dec = r.decay, sus = r.sustain, rel = r.release;

    for (auto& m : r.ccMods)
    {
        const float x = m.depth * cc[(size_t) m.cc];
        switch (m.target)
        {
            case sfz::CcMod::amplitude: amplitude *= x / 100.0f; break;
            case sfz::CcMod::volume:    vol += x; break;
            case sfz::CcMod::pan:       panPos += x; break;
            case sfz::CcMod::tune:      tuneCents += x; break;
            case sfz::CcMod::offset:    startOffset += x; break;
            case sfz::CcMod::delay:     delay += x; break;
            case sfz::CcMod::attack:    att += x; break;
            case sfz::CcMod::hold:      hold += x; break;
            case sfz::CcMod::decay:     dec += x; break;
            case sfz::CcMod::sustain:   sus += x; break;
            case sfz::CcMod::release:   rel += x; break;
            case sfz::CcMod::ampDelay:  ampDelay += x; break;
        }
    }

    float velGain = r.hasVelcurve ? r.velcurve[(size_t) juce::jlimit (0, 127, velocity)]
                                  : std::pow (velocity / 127.0f, 2.0f * juce::jlimit (0.0f, 100.0f, r.veltrack) / 100.0f);
    velGain = std::pow (juce::jmax (0.0f, velGain), velocitySensitivity->getCurrentValue());

    auto& v = *voice;
    v = Voice();
    v.active = true;
    v.region = &r;
    v.sample = r.sample.get();
    v.note = note;
    v.velocity = velocity;
    v.age = ++voiceCounter;
    v.eventId = eventId;
    v.releaseVoice = r.trigger == sfz::Trigger::release;

    v.gain = juce::Decibels::decibelsToGain (vol, -120.0f) * juce::jmax (0.0f, amplitude) / 100.0f * velGain;
    const float p = juce::jlimit (-1.0f, 1.0f, panPos / 100.0f);
    v.gainL = v.gain * juce::jmin (1.0f, 1.0f - p);
    v.gainR = v.gain * juce::jmin (1.0f, 1.0f + p);

    v.ratio = std::pow (2.0, ((note - r.keycenter) * r.keytrack + tuneCents) / 1200.0) * v.sample->sampleRate / sampleRate;
    v.pos = (double) juce::jlimit ((juce::int64) 0, r.end, (juce::int64) startOffset);
    v.delay = (juce::int64) (juce::jmax (0.0f, delay) * sampleRate) + offset;

    const float attackSeconds = juce::jmax (0.0f, att) + attack->getCurrentValue();
    v.stage = ampDelay > 0 ? Voice::Stage::delay : Voice::Stage::attack;
    v.stageSamples = (juce::int64) (ampDelay * sampleRate);
    v.attackStep = attackSeconds > 0.0005f ? (float) (1.0 / (attackSeconds * sampleRate)) : 1.0f;
    v.holdSamples = (juce::int64) (juce::jmax (0.0f, hold) * sampleRate);
    v.sustainLevel = juce::jlimit (0.0f, 1.0f, sus / 100.0f);
    v.decayStep = coefficientFor (juce::jmax (0.001f, dec), sampleRate); // SF2-style: falls 100 dB per decay time, stops at sustain
    v.releaseSeconds = juce::jmax (0.003f, rel);

    if (r.cutoff > 0.0f && r.cutoff < sampleRate * 0.45)
    {
        const double w0 = juce::MathConstants<double>::twoPi * r.cutoff / sampleRate;
        const double q = 0.7071 * std::pow (10.0, r.resonance / 20.0);
        const double alpha = std::sin (w0) / (2.0 * q), cosw = std::cos (w0), a0 = 1.0 + alpha;
        v.filtered = true;
        v.b0 = (float) ((1.0 - cosw) / 2.0 / a0);
        v.b1 = (float) ((1.0 - cosw) / a0);
        v.b2 = v.b0;
        v.a1 = (float) (-2.0 * cosw / a0);
        v.a2 = (float) ((1.0 - alpha) / a0);
    }
}

void ClaudeSamplerPlugin::releaseVoice (Voice& v, bool fast)
{
    if (v.released)
        return;

    v.released = true;
    const float seconds = fast ? fastRelease : v.releaseSeconds * release->getCurrentValue();
    v.releaseCoef = coefficientFor (juce::jmax (0.003f, seconds), sampleRate);

    // A voice that hasn't made a sound yet switches to its release once its attack starts,
    // so very short notes still play.
    if (v.stage == Voice::Stage::delay || (v.stage == Voice::Stage::attack && v.level < 0.001f))
        return;

    v.stage = Voice::Stage::release;
}

//==============================================================================
void ClaudeSamplerPlugin::renderVoices (juce::AudioBuffer<float>& buffer, int start, int num)
{
    auto* left = buffer.getWritePointer (0, start);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;

    const double tuneCents = tune->getCurrentValue() * 100.0;

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const double bend = pitchBend >= 0 ? pitchBend * v.region->bendUp : -pitchBend * v.region->bendDown;
        renderVoice (v, left, right, num, std::pow (2.0, (tuneCents + bend) / 1200.0));
    }

    const float gain = juce::Decibels::decibelsToGain (volume->getCurrentValue(), -120.0f);
    const float balance = pan->getCurrentValue();
    const float gl = gain * juce::jmin (1.0f, 1.0f - balance), gr = gain * juce::jmin (1.0f, 1.0f + balance);

    juce::FloatVectorOperations::multiply (left, gl, num);
    if (right != nullptr)
        juce::FloatVectorOperations::multiply (right, gr, num);
}

void ClaudeSamplerPlugin::renderVoice (Voice& v, float* left, float* right, int num, double pitchFactor)
{
    auto& r = *v.region;
    auto& s = *v.sample;
    const bool stereo = s.channels > 1;
    const double step = v.ratio * pitchFactor;
    const juce::int64 loopLength = r.loopEnd - r.loopStart + 1;

    for (int i = 0; i < num; ++i)
    {
        if (v.delay > 0)
        {
            --v.delay;
            continue;
        }

        switch (v.stage)
        {
            case Voice::Stage::delay:
                if (--v.stageSamples <= 0) v.stage = Voice::Stage::attack;
                break;
            case Voice::Stage::attack:
                v.level = juce::jmin (1.0f, v.level + v.attackStep);
                if (v.released) v.stage = Voice::Stage::release;
                else if (v.level >= 1.0f) { v.stage = Voice::Stage::hold; v.stageSamples = v.holdSamples; }
                break;
            case Voice::Stage::hold:
                if (--v.stageSamples <= 0) v.stage = Voice::Stage::decay;
                break;
            case Voice::Stage::decay:
                v.level *= v.decayStep;
                if (v.level <= v.sustainLevel) { v.level = v.sustainLevel; v.stage = Voice::Stage::sustain; }
                break;
            case Voice::Stage::sustain:
                if (v.sustainLevel < silence) { v.active = false; return; }
                break;
            case Voice::Stage::release:
                v.level *= v.releaseCoef;
                if (v.level < silence) { v.active = false; return; }
                break;
        }

        const bool looping = ! v.releaseVoice
                             && (r.loopMode == sfz::LoopMode::continuous || (r.loopMode == sfz::LoopMode::sustain && ! v.released));

        const auto idx = (juce::int64) v.pos;
        const float frac = (float) (v.pos - (double) idx);

        auto wrap = [&] (juce::int64 n) -> juce::int64
        {
            if (looping && n > r.loopEnd)
                return r.loopStart + (n - r.loopEnd - 1) % loopLength;
            return n > r.end ? -1 : n;
        };

        const juce::int64 n0 = wrap (idx - 1), n1 = wrap (idx), n2 = wrap (idx + 1), n3 = wrap (idx + 2);

        auto interpolate = [&] (int c)
        {
            const float x0 = s.get (n0, c), x1 = s.get (n1, c), x2 = s.get (n2, c), x3 = s.get (n3, c);
            const float c1 = 0.5f * (x2 - x0);
            const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
            const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
            return ((c3 * frac + c2) * frac + c1) * frac + x1;
        };

        float l = interpolate (0);
        float rr = stereo ? interpolate (1) : l;

        if (v.filtered)
        {
            auto biquad = [&v] (float x, int c)
            {
                const float y = v.b0 * x + v.z1[c];
                v.z1[c] = v.b1 * x - v.a1 * y + v.z2[c];
                v.z2[c] = v.b2 * x - v.a2 * y;
                return y;
            };
            l = biquad (l, 0);
            rr = stereo ? biquad (rr, 1) : l;
        }

        left[i] += l * v.level * v.gainL;
        if (right != nullptr)
            right[i] += rr * v.level * v.gainR;

        v.pos += step;

        if (looping)
        {
            while (v.pos > (double) r.loopEnd + 1.0)
                v.pos -= (double) loopLength;
        }
        else if (v.pos > (double) r.end)
        {
            v.active = false;
            return;
        }
    }
}
