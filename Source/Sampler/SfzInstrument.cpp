#include "SfzInstrument.h"

#include <map>
#include <mutex>

namespace sfz
{
//==============================================================================
// Samples
namespace
{
    uint32_t readU32 (const uint8_t* p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }
    uint16_t readU16 (const uint8_t* p) { return (uint16_t) (p[0] | p[1] << 8); }

    // Maps a PCM/float WAV in place. Returns false for anything it can't read directly.
    bool mapWav (Sample& s)
    {
        auto mapped = std::make_unique<juce::MemoryMappedFile> (s.file, juce::MemoryMappedFile::readOnly, false);
        auto* base = static_cast<const uint8_t*> (mapped->getData());
        const auto size = (juce::int64) mapped->getSize();

        if (base == nullptr || size < 44 || memcmp (base, "RIFF", 4) != 0 || memcmp (base + 8, "WAVE", 4) != 0)
            return false;

        int fmtTag = 0, bits = 0;
        const uint8_t* dataStart = nullptr;
        juce::int64 dataBytes = 0;

        for (juce::int64 pos = 12; pos + 8 <= size;)
        {
            auto* chunk = base + pos;
            juce::int64 len = readU32 (chunk + 4);
            auto* body = chunk + 8;
            const auto avail = size - (pos + 8);

            if (memcmp (chunk, "fmt ", 4) == 0 && len >= 16 && avail >= 16)
            {
                fmtTag = readU16 (body);
                s.channels = readU16 (body + 2);
                s.sampleRate = readU32 (body + 4);
                bits = readU16 (body + 14);

                if (fmtTag == 0xFFFE && len >= 26 && avail >= 26)
                    fmtTag = readU16 (body + 24); // WAVE_FORMAT_EXTENSIBLE: sub-format GUID starts with the tag
            }
            else if (memcmp (chunk, "data", 4) == 0)
            {
                dataStart = body;
                dataBytes = juce::jmin (len, avail); // also copes with 0xFFFFFFFF sizes
            }
            else if (memcmp (chunk, "smpl", 4) == 0 && len >= 36 && avail >= 36)
            {
                const auto unity = (int) readU32 (body + 12);
                if (unity > 0 && unity < 128)
                    s.rootNote = unity;

                if (readU32 (body + 28) > 0 && len >= 60 && avail >= 60)
                {
                    s.loopStart = readU32 (body + 44);
                    s.loopEnd = readU32 (body + 48);
                }
            }

            pos += 8 + len + (len & 1);
        }

        if (dataStart == nullptr || s.channels <= 0 || s.sampleRate <= 0)
            return false;

        if (fmtTag == 1 && bits == 16)       s.format = Sample::Format::int16;
        else if (fmtTag == 1 && bits == 24)  s.format = Sample::Format::int24;
        else if (fmtTag == 1 && bits == 32)  s.format = Sample::Format::int32;
        else if (fmtTag == 1 && bits == 8)   s.format = Sample::Format::uint8;
        else if (fmtTag == 3 && bits == 32)  s.format = Sample::Format::float32;
        else return false;

        s.frames = dataBytes / (s.channels * (bits / 8));
        s.data = dataStart;
        s.mapped = std::move (mapped);
        return s.frames > 0;
    }

    bool decode (Sample& s, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (s.file));

        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > 48000 * 600)
        {
            error = "Can't read " + s.file.getFullPathName();
            return false;
        }

        s.channels = juce::jlimit (1, 2, (int) reader->numChannels);
        s.frames = reader->lengthInSamples;
        s.sampleRate = reader->sampleRate;

        juce::AudioBuffer<float> buffer (s.channels, (int) s.frames);
        reader->read (&buffer, 0, (int) s.frames, 0, true, s.channels > 1);

        s.decoded.resize ((size_t) (s.frames * s.channels));
        for (int c = 0; c < s.channels; ++c)
            for (int i = 0; i < (int) s.frames; ++i)
                s.decoded[(size_t) (i * s.channels + c)] = buffer.getSample (c, i);

        s.format = Sample::Format::float32;
        s.data = s.decoded.data();
        return true;
    }

    std::mutex cacheLock;
    std::map<juce::String, std::weak_ptr<const Sample>> cache;
}

void Sample::touch() const
{
    if (mapped == nullptr)
        return;

    auto* p = static_cast<const volatile uint8_t*> (mapped->getData());
    const auto size = mapped->getSize();
    uint8_t sum = 0;

    for (size_t i = 0; i < size; i += 4096)
        sum = (uint8_t) (sum + p[i]);

    juce::ignoreUnused (sum);
}

std::shared_ptr<const Sample> loadSample (const juce::File& file, juce::String& error)
{
    const auto key = file.getFullPathName().toLowerCase();

    std::lock_guard<std::mutex> l (cacheLock);

    if (auto existing = cache[key].lock())
        return existing;

    if (! file.existsAsFile())
    {
        error = "Missing sample " + file.getFullPathName();
        return {};
    }

    auto s = std::make_shared<Sample>();
    s->file = file;

    if (! mapWav (*s))
    {
        *s = Sample();
        s->file = file;

        if (! decode (*s, error))
            return {};
    }

    cache[key] = s;
    return s;
}

//==============================================================================
// Parsing
int noteNumber (const juce::String& text)
{
    auto s = text.trim().toLowerCase();

    if (s.isEmpty())
        return -1;

    if (s.containsOnly ("-0123456789."))
        return s.getIntValue();

    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    const auto letter = s[0];

    if (letter < 'a' || letter > 'g')
        return -1;

    int n = base[letter - 'a'];
    int i = 1;

    if (s[i] == '#') { ++n; ++i; }
    else if (s[i] == 'b' && s.length() > 2) { --n; ++i; }

    auto octave = s.substring (i);
    if (! octave.containsOnly ("-0123456789") || octave.isEmpty())
        return -1;

    return n + (octave.getIntValue() + 1) * 12; // c4 = 60
}

namespace
{
    using Opcodes = std::map<juce::String, juce::String>;

    juce::String preprocess (const juce::File& f, const juce::File& rootDir, Opcodes& defines, int depth)
    {
        auto text = f.loadFileAsString();

        // block comments
        for (int start; (start = text.indexOf ("/*")) >= 0;)
        {
            const auto end = text.indexOf (start + 2, "*/");
            text = text.substring (0, start) + " " + (end < 0 ? juce::String() : text.substring (end + 2));
        }

        juce::String out;
        out.preallocateBytes ((size_t) text.getNumBytesAsUTF8() + 16);

        for (auto line : juce::StringArray::fromLines (text))
        {
            const auto comment = line.indexOf ("//");
            if (comment >= 0)
                line = line.substring (0, comment);

            auto trimmed = line.trim();

            if (trimmed.startsWith ("#define"))
            {
                auto rest = trimmed.substring (7).trim();
                auto name = rest.upToFirstOccurrenceOf (" ", false, false).trim();
                if (name.startsWith ("$"))
                    defines[name] = rest.fromFirstOccurrenceOf (" ", false, false).trim();
                continue;
            }

            if (trimmed.startsWith ("#include"))
            {
                auto inc = trimmed.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false);
                auto incFile = rootDir.getChildFile (inc.replaceCharacter ('\\', '/'));

                if (depth < 8 && incFile.existsAsFile())
                    out << preprocess (incFile, rootDir, defines, depth + 1) << "\n";
                continue;
            }

            if (line.containsChar ('$'))
            {
                // longest names first so $VAR10 isn't replaced by $VAR1
                std::vector<std::pair<juce::String, juce::String>> sorted (defines.begin(), defines.end());
                std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.first.length() > b.first.length(); });

                for (auto& [k, v] : sorted)
                    line = line.replace (k, v);
            }

            out << line << "\n";
        }

        return out;
    }

    bool isNameChar (juce::juce_wchar c) { return juce::CharacterFunctions::isLetterOrDigit (c) || c == '_' || c == '$'; }

    struct Token { bool header; juce::String name; int start, end; };

    // Headers (<region>) and opcodes (name=); an opcode's value runs to the next token.
    std::vector<Token> tokenise (const juce::String& text)
    {
        std::vector<Token> tokens;
        auto* p = text.toRawUTF8();
        const int n = (int) strlen (p);

        for (int i = 0; i < n;)
        {
            const char c = p[i];

            if (c == '<')
            {
                int j = i + 1;
                while (j < n && p[j] != '>' && p[j] != '\n') ++j;
                if (j < n && p[j] == '>')
                {
                    tokens.push_back ({ true, juce::String::fromUTF8 (p + i + 1, j - i - 1).trim().toLowerCase(), i, j + 1 });
                    i = j + 1;
                    continue;
                }
            }

            if (isNameChar ((juce::juce_wchar) (uint8_t) c) && (i == 0 || p[i - 1] == ' ' || p[i - 1] == '\t' || p[i - 1] == '\n' || p[i - 1] == '\r' || p[i - 1] == '>'))
            {
                int j = i;
                while (j < n && isNameChar ((juce::juce_wchar) (uint8_t) p[j])) ++j;
                if (j < n && p[j] == '=')
                {
                    tokens.push_back ({ false, juce::String::fromUTF8 (p + i, j - i).toLowerCase(), i, j + 1 });
                    i = j + 1;
                    continue;
                }
                i = j;
                continue;
            }

            ++i;
        }

        return tokens;
    }

    float num (const Opcodes& o, const char* name, float fallback)
    {
        auto it = o.find (name);
        return it == o.end() ? fallback : it->second.getFloatValue();
    }

    bool has (const Opcodes& o, const char* name) { return o.find (name) != o.end(); }

    juce::String str (const Opcodes& o, const char* name)
    {
        auto it = o.find (name);
        return it == o.end() ? juce::String() : it->second;
    }

    int note (const Opcodes& o, const char* name, int fallback)
    {
        auto it = o.find (name);
        if (it == o.end())
            return fallback;
        auto v = noteNumber (it->second);
        return v < 0 ? fallback : v;
    }

    bool ccTarget (const juce::String& name, CcMod::Target& t)
    {
        static const std::pair<const char*, CcMod::Target> names[] = {
            { "amplitude", CcMod::amplitude }, { "volume", CcMod::volume }, { "gain", CcMod::volume },
            { "pan", CcMod::pan }, { "tune", CcMod::tune }, { "pitch", CcMod::tune },
            { "offset", CcMod::offset }, { "delay", CcMod::delay },
            { "ampeg_attack", CcMod::attack }, { "ampeg_hold", CcMod::hold }, { "ampeg_decay", CcMod::decay },
            { "ampeg_sustain", CcMod::sustain }, { "ampeg_release", CcMod::release }, { "ampeg_delay", CcMod::ampDelay },
        };

        for (auto& [n, target] : names)
            if (name == n) { t = target; return true; }

        return false;
    }

    // "pan_oncc10" -> ("pan", 10); also the SFZ v1 spellings "ampeg_attackcc1" and "volume_cc7".
    bool splitCcOpcode (const juce::String& key, juce::String& target, int& cc)
    {
        if (key.startsWith ("locc") || key.startsWith ("hicc") || key.startsWith ("on_") || key.startsWith ("set_cc")
            || key.startsWith ("label_cc") || key.startsWith ("amp_velcurve"))
            return false;

        const auto at = key.lastIndexOf ("cc");
        if (at <= 0 || ! key.substring (at + 2).containsOnly ("0123456789") || key.length() == at + 2)
            return false;

        cc = key.substring (at + 2).getIntValue();
        target = key.substring (0, at);
        if (target.endsWith ("on")) target = target.dropLastCharacters (2);
        if (target.endsWith ("_")) target = target.dropLastCharacters (1);
        return cc >= 0 && cc < 256;
    }

    LoopMode loopModeFrom (const juce::String& s)
    {
        if (s == "no_loop")         return LoopMode::noLoop;
        if (s == "one_shot")        return LoopMode::oneShot;
        if (s == "loop_continuous") return LoopMode::continuous;
        if (s == "loop_sustain")    return LoopMode::sustain;
        return LoopMode::unset;
    }
}

std::shared_ptr<const Instrument> loadInstrument (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Instrument file not found: " + file.getFullPathName();
        return {};
    }

    auto inst = std::make_shared<Instrument>();
    inst->file = file;
    inst->ccDefaults[7] = 100.0f / 127.0f;
    inst->ccDefaults[10] = 64.0f / 127.0f;
    inst->ccDefaults[11] = 1.0f;

    Opcodes defines;
    const auto dir = file.getParentDirectory();
    const auto text = preprocess (file, dir, defines, 0);
    const auto tokens = tokenise (text);

    Opcodes control, global, master, group, region;
    std::vector<Opcodes> merged;
    Opcodes* current = nullptr;
    juce::String section;

    auto close = [&]
    {
        if (section == "region")
        {
            Opcodes r = global;
            for (auto* level : { &master, &group, &region })
                for (auto& [k, v] : *level)
                    r[k] = v;
            merged.push_back (std::move (r));
        }
    };

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        auto& t = tokens[i];

        if (t.header)
        {
            close();
            section = t.name;

            if (section == "control")      current = &control;
            else if (section == "global")  { global.clear(); master.clear(); group.clear(); current = &global; }
            else if (section == "master")  { master.clear(); group.clear(); current = &master; }
            else if (section == "group")   { group.clear(); current = &group; }
            else if (section == "region")  { region.clear(); current = &region; }
            else                           current = nullptr; // <curve>, <effect>, <midi>...
            continue;
        }

        const int valueEnd = i + 1 < tokens.size() ? tokens[i + 1].start : (int) text.getNumBytesAsUTF8();
        auto value = juce::String::fromUTF8 (text.toRawUTF8() + t.end, juce::jmax (0, valueEnd - t.end)).trim();

        if (current != nullptr)
            (*current)[t.name] = value;
    }
    close();

    for (auto& [k, v] : control)
        if (k.startsWith ("set_cc") && k.substring (6).containsOnly ("0123456789"))
            inst->ccDefaults[(size_t) juce::jlimit (0, 255, k.substring (6).getIntValue())] = juce::jlimit (0.0f, 127.0f, v.getFloatValue()) / 127.0f;

    const auto defaultPath = str (control, "default_path").replaceCharacter ('\\', '/');
    juce::StringArray missing;

    for (auto& o : merged)
    {
        auto sampleName = str (o, "sample");
        if (sampleName.isEmpty() || sampleName.startsWith ("*") || has (o, "on_locc") || has (o, "on_hicc"))
            continue;

        bool ccTriggered = false;
        for (auto& [k, v] : o)
            if (k.startsWith ("on_locc") || k.startsWith ("on_hicc"))
                ccTriggered = true;
        if (ccTriggered)
            continue;

        auto sampleFile = dir.getChildFile ((defaultPath + sampleName).replaceCharacter ('\\', '/'));
        juce::String sampleError;
        auto sample = loadSample (sampleFile, sampleError);

        if (sample == nullptr)
        {
            if (missing.size() < 5) missing.add (sampleError);
            continue;
        }

        Region r;
        r.sample = sample;

        auto trig = str (o, "trigger").toLowerCase();
        r.trigger = trig == "release" || trig == "release_key" ? Trigger::release
                  : trig == "first" ? Trigger::first
                  : trig == "legato" ? Trigger::legato : Trigger::attack;

        const int key = note (o, "key", -1);
        r.lokey = note (o, "lokey", key >= 0 ? key : 0);
        r.hikey = note (o, "hikey", key >= 0 ? key : 127);
        r.lovel = juce::jlimit (0, 127, (int) num (o, "lovel", 1));
        r.hivel = juce::jlimit (0, 127, (int) num (o, "hivel", 127));

        if (str (o, "pitch_keycenter").equalsIgnoreCase ("sample"))
            r.keycenter = sample->rootNote >= 0 ? sample->rootNote : 60;
        else
            r.keycenter = note (o, "pitch_keycenter", key >= 0 ? key : (sample->rootNote >= 0 ? sample->rootNote : 60));

        r.keytrack = num (o, "pitch_keytrack", 100.0f);
        r.tune = num (o, "tune", num (o, "pitch", 0.0f)) + 100.0f * num (o, "transpose", 0.0f);
        r.volume = num (o, "volume", num (o, "gain", 0.0f));
        r.amplitude = num (o, "amplitude", 100.0f);
        r.pan = num (o, "pan", 0.0f);
        r.veltrack = num (o, "amp_veltrack", 100.0f);

        // amp_velcurve_N: gain points, linearly interpolated between (0,0) and (127,1)
        std::map<int, float> points;
        for (auto& [k, v] : o)
            if (k.startsWith ("amp_velcurve_"))
                points[juce::jlimit (0, 127, k.substring (13).getIntValue())] = juce::jlimit (0.0f, 1.0f, v.getFloatValue());

        if (! points.empty())
        {
            r.hasVelcurve = true;
            points.emplace (0, 0.0f);
            points.emplace (127, 1.0f);

            for (int v = 0; v < 128; ++v)
            {
                auto hi = points.lower_bound (v);
                if (hi->first == v) { r.velcurve[(size_t) v] = hi->second; continue; }
                auto lo = std::prev (hi);
                r.velcurve[(size_t) v] = lo->second + (hi->second - lo->second) * (float) (v - lo->first) / (float) (hi->first - lo->first);
            }
        }

        r.delay = num (o, "delay", 0.0f);
        r.ampDelay = num (o, "ampeg_delay", 0.0f);
        r.attack = num (o, "ampeg_attack", 0.0f);
        r.hold = num (o, "ampeg_hold", 0.0f);
        r.decay = num (o, "ampeg_decay", 0.0f);
        r.sustain = num (o, "ampeg_sustain", 100.0f);
        r.release = num (o, "ampeg_release", 0.001f);

        r.offset = (juce::int64) num (o, "offset", 0.0f);
        r.end = has (o, "end") ? (juce::int64) num (o, "end", 0.0f) : sample->frames - 1;
        if (r.end <= 0 || r.end >= sample->frames) r.end = sample->frames - 1;
        if (r.offset < 0 || r.offset >= r.end) r.offset = 0;

        const bool loopGiven = has (o, "loop_start") || has (o, "loopstart") || has (o, "loop_end") || has (o, "loopend");
        r.loopStart = (juce::int64) num (o, "loop_start", num (o, "loopstart", (float) sample->loopStart));
        r.loopEnd = (juce::int64) num (o, "loop_end", num (o, "loopend", (float) sample->loopEnd));
        const bool loopValid = r.loopStart >= 0 && r.loopEnd > r.loopStart + 8 && r.loopEnd <= r.end;

        r.loopMode = loopModeFrom (str (o, "loop_mode").isNotEmpty() ? str (o, "loop_mode") : str (o, "loopmode"));
        if (r.loopMode == LoopMode::unset)
            r.loopMode = (loopValid && (loopGiven || sample->loopEnd > 0)) ? LoopMode::continuous : LoopMode::noLoop;
        if ((r.loopMode == LoopMode::continuous || r.loopMode == LoopMode::sustain) && ! loopValid)
            r.loopMode = LoopMode::noLoop;

        r.rtDecay = num (o, "rt_decay", 0.0f);
        r.group = (juce::int64) num (o, "group", num (o, "polyphony_group", 0.0f));
        r.hasOffBy = has (o, "off_by") || has (o, "offby");
        r.offBy = (juce::int64) num (o, "off_by", num (o, "offby", 0.0f));
        r.offModeFast = ! str (o, "off_mode").equalsIgnoreCase ("normal");

        r.seqLength = juce::jmax (1, (int) num (o, "seq_length", 1.0f));
        r.seqPosition = juce::jlimit (1, r.seqLength, (int) num (o, "seq_position", 1.0f));
        r.lorand = num (o, "lorand", 0.0f);
        r.hirand = num (o, "hirand", 1.0f);
        r.swLast = note (o, "sw_last", -1);
        r.bendUp = num (o, "bend_up", 200.0f);
        r.bendDown = num (o, "bend_down", -200.0f);

        auto filType = str (o, "fil_type").toLowerCase();
        if (has (o, "cutoff") && (filType.isEmpty() || filType.startsWith ("lpf")))
        {
            r.cutoff = juce::jlimit (20.0f, 20000.0f, num (o, "cutoff", 20000.0f));
            r.resonance = juce::jlimit (0.0f, 24.0f, num (o, "resonance", 0.0f));
        }

        std::map<int, CcRange> ranges;
        for (auto& [k, v] : o)
        {
            if ((k.startsWith ("locc") || k.startsWith ("hicc")) && k.substring (4).containsOnly ("0123456789"))
            {
                const int cc = juce::jlimit (0, 255, k.substring (4).getIntValue());
                auto& range = ranges.try_emplace (cc, CcRange { cc, 0.0f, 127.0f }).first->second;
                (k.startsWith ("lo") ? range.lo : range.hi) = v.getFloatValue();
                continue;
            }

            juce::String target;
            int cc;
            CcMod::Target t;
            if (splitCcOpcode (k, target, cc) && ccTarget (target, t))
                r.ccMods.push_back ({ t, cc, v.getFloatValue() });
        }
        for (auto& [cc, range] : ranges)
            r.ccRanges.push_back (range);

        for (auto* name : { "sw_lokey", "sw_hikey" })
            if (auto k = note (o, name, -1); k >= 0)
            {
                inst->swLokey = inst->swLokey < 0 ? k : juce::jmin (inst->swLokey, k);
                inst->swHikey = juce::jmax (inst->swHikey, k);
            }
        if (auto d = note (o, "sw_default", -1); d >= 0)
            inst->swDefault = d;

        if (r.lovel > r.hivel || r.hirand <= r.lorand || r.lokey > r.hikey)
            continue; // can never sound (e.g. placeholder regions with hivel=0)

        if (r.trigger != Trigger::release)
        {
            inst->lowestKey = juce::jmin (inst->lowestKey, r.lokey);
            inst->highestKey = juce::jmax (inst->highestKey, r.hikey);
        }

        inst->regions.push_back (std::move (r));
    }

    // Regions that name a keyswitch but no sw_lokey/hikey range: use their sw_last notes as the range.
    for (auto& r : inst->regions)
        if (r.swLast >= 0 && inst->swLokey < 0)
        {
            int lo = 127, hi = 0;
            for (auto& q : inst->regions)
                if (q.swLast >= 0) { lo = juce::jmin (lo, q.swLast); hi = juce::jmax (hi, q.swLast); }
            inst->swLokey = lo;
            inst->swHikey = hi;
            break;
        }

    // No sw_default: if some regions ignore keyswitches they play on their own until a switch is
    // pressed (Emilyguitar's power chords); otherwise start on the lowest switch so it isn't silent.
    if (inst->hasKeyswitches() && inst->swDefault < 0)
    {
        int lowest = 128;
        bool someIgnoreSwitches = false;
        for (auto& r : inst->regions)
        {
            if (r.swLast >= 0) lowest = juce::jmin (lowest, r.swLast);
            else if (r.trigger != Trigger::release) someIgnoreSwitches = true;
        }
        inst->swDefault = someIgnoreSwitches ? -1 : (lowest < 128 ? lowest : inst->swLokey);
    }

    if (! missing.isEmpty())
        inst->warnings.addArray (missing);

    if (inst->regions.empty())
    {
        error = "No playable regions in " + file.getFileName() + (missing.isEmpty() ? juce::String() : " (" + missing[0] + ")");
        return {};
    }

    return inst;
}

void Instrument::touchSamples() const
{
    std::vector<const Sample*> seen;
    for (auto& r : regions)
        if (std::find (seen.begin(), seen.end(), r.sample.get()) == seen.end())
        {
            seen.push_back (r.sample.get());
            r.sample->touch();
        }
}
}
