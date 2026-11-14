#pragma once

#include <JuceHeader.h>

// A multi-sample instrument read from an .sfz file: regions with key and
// velocity ranges, round-robins, keyswitches, loops, envelopes and release
// samples. Loaded once on the message thread, then shared read-only with the
// audio thread (and between tracks that use the same file).
namespace sfz
{
    // One WAV file, memory-mapped so large libraries (a grand piano is over
    // 1 GB) don't have to sit in RAM. Non-WAV files are decoded into memory.
    struct Sample
    {
        juce::File file;
        int channels = 0;
        juce::int64 frames = 0;
        double sampleRate = 44100.0;
        juce::int64 loopStart = -1, loopEnd = -1; // from the WAV smpl chunk; loopEnd inclusive
        int rootNote = -1;                        // from the WAV smpl chunk

        // frame i, channel c (c is clamped to the available channels); 0 outside the file
        float get (juce::int64 i, int c) const noexcept
        {
            if (i < 0 || i >= frames)
                return 0.0f;

            c = juce::jmin (c, channels - 1);

            switch (format)
            {
                case Format::int16:   return reinterpret_cast<const int16_t*> (data)[i * channels + c] * (1.0f / 32768.0f);
                case Format::int24:
                {
                    auto p = static_cast<const uint8_t*> (data) + (i * channels + c) * 3;
                    return (float) (int32_t ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) >> 8) * (1.0f / 8388608.0f);
                }
                case Format::int32:   return reinterpret_cast<const int32_t*> (data)[i * channels + c] * (1.0f / 2147483648.0f);
                case Format::float32: return reinterpret_cast<const float*> (data)[i * channels + c];
                case Format::uint8:   return (static_cast<const uint8_t*> (data)[i * channels + c] - 128) * (1.0f / 128.0f);
            }
            return 0.0f;
        }

        // Reads every page once on a background thread so the first note doesn't stall on disk.
        void touch() const;

        enum class Format { int16, int24, int32, float32, uint8 };
        Format format = Format::int16;
        const void* data = nullptr;
        std::unique_ptr<juce::MemoryMappedFile> mapped;
        std::vector<float> decoded; // interleaved, used when the file isn't a plain WAV
    };

    // Returns a shared sample (one copy per file across the whole app), or nullptr with an error.
    std::shared_ptr<const Sample> loadSample (const juce::File&, juce::String& error);

    enum class Trigger { attack, release, first, legato };
    enum class LoopMode { unset, noLoop, oneShot, continuous, sustain };

    // An opcode whose value depends on a MIDI CC (e.g. pan_oncc10=200).
    struct CcMod { enum Target { amplitude, volume, pan, tune, offset, delay, attack, hold, decay, sustain, release, ampDelay };
                   Target target; int cc; float depth; };

    struct CcRange { int cc; float lo, hi; };

    struct Region
    {
        std::shared_ptr<const Sample> sample;
        int lokey = 0, hikey = 127, lovel = 1, hivel = 127;
        int keycenter = 60;
        float keytrack = 100.0f;    // cents per key
        float tune = 0.0f;          // cents (includes transpose)
        float volume = 0.0f;        // dB
        float amplitude = 100.0f;   // percent
        float pan = 0.0f;           // -100..100
        float veltrack = 100.0f;
        std::array<float, 128> velcurve {}; // gain per velocity; empty unless hasVelcurve
        bool hasVelcurve = false;

        float delay = 0.0f;
        float ampDelay = 0.0f, attack = 0.0f, hold = 0.0f, decay = 0.0f, sustain = 100.0f, release = 0.001f;

        LoopMode loopMode = LoopMode::noLoop;
        juce::int64 loopStart = 0, loopEnd = 0, offset = 0, end = 0; // end/loopEnd inclusive

        Trigger trigger = Trigger::attack;
        float rtDecay = 0.0f;       // dB per second held (release samples)

        juce::int64 group = 0, offBy = 0;
        bool hasOffBy = false, offModeFast = true;

        int seqLength = 1, seqPosition = 1;
        float lorand = 0.0f, hirand = 1.0f;
        std::vector<CcRange> ccRanges;
        std::vector<CcMod> ccMods;
        int swLast = -1;
        float bendUp = 200.0f, bendDown = -200.0f;

        float cutoff = 0.0f;        // Hz, 0 = no filter
        float resonance = 0.0f;     // dB
    };

    struct Instrument
    {
        juce::File file;
        std::vector<Region> regions;
        std::array<float, 256> ccDefaults {}; // 0..1
        int swLokey = -1, swHikey = -1, swDefault = -1;
        int lowestKey = 127, highestKey = 0;
        juce::StringArray warnings;

        bool hasKeyswitches() const noexcept { return swLokey >= 0; }
        void touchSamples() const;
    };

    // Parses an .sfz file and loads its samples. Returns nullptr (and an error) when nothing playable is found.
    std::shared_ptr<const Instrument> loadInstrument (const juce::File&, juce::String& error);

    int noteNumber (const juce::String&); // "60", "c4", "f#3", "eb2"; -1 if not a note
}
