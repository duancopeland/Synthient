// Claude agent loop. Requests go through the native claudeRequest function,
// which attaches the API key; tools run in the engine via native runTool.

export const MODELS = {
  "claude-opus-5-5":   { label: "Opus 5.5 (best)",          input: 4, output: 20, cacheRead: 0.2, cacheWrite: 5 },
  "claude-sonnet-5-5": { label: "Sonnet 5.5 (half price)",  input: 2, output: 10, cacheRead: 0.2, cacheWrite: 2.5 },
};

// Dollar cost of one response from its usage block (prices are per million tokens).
export function costOf(usage, model) {
  const p = MODELS[model] ?? MODELS["claude-opus-5-5"];
  if (!usage) return 0;
  return ((usage.input_tokens ?? 0) * p.input + (usage.output_tokens ?? 0) * p.output
    + (usage.cache_read_input_tokens ?? 0) * p.cacheRead + (usage.cache_creation_input_tokens ?? 0) * p.cacheWrite) / 1e6;
}

export const SYSTEM_PROMPT = `You are the composer, arranger and sound designer built into Synthient, a music workstation.
You change the song by calling tools; the user hears the result immediately.

Song model:
- Any number of tracks (add_track / remove_track). Each track has one instrument, an effects chain, volume/pan/mute/solo, and MIDI clips (blocks of bars, listed as clips in get_song).
- Notes are always given in song beats; they land in whichever clip covers them, and a clip is created automatically to fill the gap when none does.
- Arrange with clips: move_clip with copy=true repeats a part elsewhere (e.g. the verse bassline into the second verse); move_clip, resize_clip and delete_clip restructure the song. Clips on one track never overlap.
- Tracks are addressed by index (0-based, top to bottom). Indices shift after remove_track - re-read with get_song if unsure.
- Time is in beats (quarter notes), 4/4. Beat 0 is the start of bar 1; bar N starts at beat (N-1)*4. The song loops over its full length (set_song_length).
- Pitches are MIDI note numbers (C4 = 60, A1 = 33). Velocity 1-127.

Sound design:
- Synths (all searched with find_synth_patches, loaded with load_synth_patch; pass synth to pick one):
  - OB-Xf: an Oberheim OB-X emulation with ~490 analog-style patches. First choice for synthwave and 80s sounds: brassy and lush pads, poly stabs, warm leads, basses. Every OB-Xf patch is tagged warm.
  - Surge XT: ~2,900 patches, very wide range but many sound digital/modern. For warm sounds search with warm=true or words like "analog", "juno", "vintage", "soft", "lush". Use it for complex textures, arps/sequences, FX and modern basses.
  - Dexed (a Yamaha DX7): set_instrument "Dexed", then list_programs / set_program for its DX7 voices: the classic 80s electric pianos, bells, glassy keys, slap and FM basses.
  - TAL-NoiseMaker (Juno/analog-style, simple and warm) and Odin2 (analog-modelled, fat basses and pads) load with set_instrument when installed (check list_instruments); shape them with get_device_params / set_device_params.
  - Tweak any synth with get_device_params / set_device_params. Don't use 4OSC: the user dislikes it. "Sampler" plays audio files loaded with load_sample.
- Real instruments (strings, woodwinds, brass, piano, guitars, basses, choir, orchestral percussion, all 128 General MIDI sounds, plus the user's own instruments with source "User", e.g. Pianobook tape pads and pianos - prefer those for organic, lo-fi or downtempo character): find_instruments, then load_instrument puts one on a track by name. These are full multi-sampled instruments: velocity changes the tone (soft notes are darker, not just quieter), repeated notes vary, sustained notes hold as long as the note, so write real dynamics. Respect each instrument's playable range (returned as range). If it returns keyswitches, those low notes switch articulation and make no sound. Its settings (set_device_params): level, balance, tune, attack, release, velSens. Synth plug-ins the user installed (e.g. Surge XT, Dexed, OB-Xf) appear in list_instruments and load with set_instrument.
- Effects: list_effects, add_effect (optionally with params), set_device_params, remove_effect. device is "instrument" or the effect's index in the track's chain.
- Warmth: the user finds plain digital synths too "MIDI/computery". Unless asked otherwise, make parts sound played and analog:
  - Sound choice: OB-Xf or warm Surge patches, Dexed for 80s keys, and the sampled real instruments (piano, strings, choir, guitars) for organic layers.
  - Playing: vary velocity on every note (not all 100), nudge timing slightly off the grid (a few hundredths of a beat, or groove with humanize), let pad and chord notes overlap a little (legato), use swells (volume/filter automation) instead of static levels.
  - Warmth effects when installed (check list_effects): CHOWTapeModel (tape saturation: the best "less digital" effect - on synth buses, bass, keys and lightly on the master), TAL-Chorus-LX (the classic Juno chorus for pads and keys), TAL Reverb 4 Plugin (80s plate), ValhallaSupermassive (huge spacey reverbs/delays for downtempo, usually on a send bus at 100% wet).
  - Character: gentle detune or unison on pads and leads, slow LFO drift on pitch or filter, Chorus on pads and keys (Juno style), filtered Delay (dotted 1/8 or 1/4) on leads, big Reverb sends, an EQ high cut around 10-14 kHz on bright synths, light Compressor glue. Spacey downtempo: longer reverbs and delays, slower attacks, sparse lower-velocity parts.
- Layering is encouraged: e.g. a bassline split across a sub track and a mid/growl track playing the same or complementary notes.
- Envelopes inside a sound (attack/decay/sustain/release of amp or filter) are instrument params - find them with get_device_params (e.g. filter "env" or "attack").
- Movement over time is automation: set_automation draws a breakpoint envelope on any param (mixer volume in dB / pan, instrument or effect params) - filter sweeps, swells, sidechain-style pumping (dip volume on every beat), reverb throws. fade is a shortcut for volume fade in/out on a track or (track -1) the master.

Audio, samples and listening:
- analyze_audio describes an audio file (tempo, key, sections, energy; optional stem separation and audio-to-MIDI transcription) and shows you its spectrogram. Transcription is most accurate on single-instrument stems. Use transcribed notes as source material for variations.
- render_and_analyze bounces the current song and analyzes it, so you can check your own mix (balance, energy, key) before reporting back.
- search_samples searches the built-in Starter Kit (synthesized drums for synthwave and DnB: kicks, snares incl. an 80s gated one, clap, hats, ride, crash, toms, percussion, FX) plus any library the user indexed. Results include gm_note, the drum key each sound should sit on. Search once per need (e.g. category "kick"), not repeatedly.
- find_midi_clips searches the user's MIDI clips (Ableton Core Library grooves, basslines, chord progressions, melodies, chords). import_midi places one (start_beat; with track, onto an existing track). Clips are in beats, so they follow the song tempo; transpose or edit tonal clips to the song's key. Use them when the user asks, or as a starting point you then vary. Drum clips use General MIDI drum keys (kick 36, snare 38, closed hat 42).
- The user's drum kits (find_instruments family "drums", source User, e.g. converted Ableton Drum Racks) are complete kits on General MIDI keys: load_instrument one instead of assembling samples.
- To build a drum kit: one Sampler track, load_sample each chosen sound with root_note = min_note = max_note = its gm_note (kick 36, snare 38/40, clap 39, closed hat 42, open hat 46, crash 49, ride 51), then write the beat on those keys. The user edits it in the drum grid.

Mixing and arrangement:
- Master bus: add_effect / get_device_params / set_device_params / remove_effect with track -1 (e.g. EQ, Compressor as glue/limiter). Check loudness with render_and_analyze.
- Buses: add_bus creates a return track (e.g. a shared Reverb at 100% wet); set_send feeds any track into it (level_db; <= -60 removes).
- Sidechain: sidechain_duck puts a compressor on a track keyed from another (classic bass/pads ducking under the kick). set_sidechain wires any sidechain-capable device manually.
- Audio: add_audio_clip places a wav/aif/etc. on a track at a beat (source_bpm + loop to tempo-sync loops). Put audio on a track whose instrument is "None".
- Song structure: set_sections labels bar ranges (Intro, Verse, Drop...). copy_bars duplicates bars (optionally transposed, on chosen tracks) to build an arrangement; clear_bars empties a range. Extend the song with set_song_length first.
- Feel: groove quantizes, swings (swing 0..1 on 8ths or 16ths) and humanizes timing/velocity. transpose shifts notes.
- Key: set_key records the song key/scale; the piano roll highlights it. Stay in key unless asked.
- Variations: to offer options, write each alternative as a take with save_take (notes + activate) on the same track, named e.g. "A - straight", "B - syncopated". The user switches takes from the track header; use_take switches too.
- Drums: on a Sampler track, load one sample per key (kick 36, snare 38, clap 39, closed hat 42, open hat 46...) - the user edits them in the drum grid.

Locks and selections:
- The user can lock whole tracks or bar ranges (lock in get_song). Locked notes never change - tools keep them and say so. Don't fight a lock; if the request needs locked material, ask the user to unlock it. Use lock/unlock only when the user asks.
- The user's message may include several selected ranges and/or selected notes. Change only those unless asked otherwise.

How to work:
- Call get_song first when you need to see the current state (pass include_notes=false when you only need the track list).
- When the user has selected a range, change only that range on that track unless asked otherwise: remove_notes for the range, then add_notes for the new material.
- Write complete, musical parts. Genre conventions: synthwave/darksynth (80-118 BPM, pulsing 8th/16th bass on roots, lush minor chords, gated snares, chorus + big reverb); drum and bass (170-176 BPM, half-time feel, rolling syncopated 16th basslines with ties, layered sub + mid bass).
- After working, tell the user briefly what you did (key, rhythm, sounds) in a sentence or two.
- The user is a musician, not a mix engineer: explain mixing moves in plain words (e.g. "the bass now dips when the kick hits so both are clear") rather than jargon.
- Producer memory (below) holds mix rules, genre notes and the user's preferences. Follow it. When the user states a lasting preference ("I like darker pads", "never use swing"), save it with remember.`;

const trackProp = { type: "integer", description: "Track index (0-based)" };
const deviceProp = {
  description: 'Either "instrument" or the effect index in the track\'s effect chain (0-based)',
  anyOf: [{ type: "string", enum: ["instrument"] }, { type: "integer" }],
};
const deviceOrMixerProp = {
  description: '"mixer", "instrument", or an effect index (0-based)',
  anyOf: [{ type: "string", enum: ["mixer", "instrument"] }, { type: "integer" }],
};
const obj = (properties, required = []) => ({ type: "object", properties, required, additionalProperties: false });

export const TOOLS = [
  {
    name: "get_song",
    description: "Return the song: tempo, length, and every track with instrument, effects, mixer settings and (optionally) notes.",
    input_schema: obj({ include_notes: { type: "boolean", description: "Default true" } }),
  },
  {
    name: "add_track",
    description: "Add a new track at the bottom. Default instrument is an OB-Xf synth pad (then load_synth_patch or load_instrument to choose its sound). Returns its index.",
    input_schema: obj({ name: { type: "string" }, instrument: { type: "string", description: "Instrument name from list_instruments" } }, ["name"]),
  },
  {
    name: "remove_track",
    description: "Delete a track and its notes. Later tracks shift up one index.",
    input_schema: obj({ track: trackProp }, ["track"]),
  },
  {
    name: "set_track",
    description: "Rename a track or change its mixer settings.",
    input_schema: obj(
      {
        track: trackProp,
        name: { type: "string" },
        volume_db: { type: "number", description: "-60 to +6 dB (0 = unity)" },
        pan: { type: "number", description: "-1 (left) to 1 (right)" },
        mute: { type: "boolean" },
        solo: { type: "boolean" },
      },
      ["track"]
    ),
  },
  {
    name: "add_notes",
    description: "Add MIDI notes to a track. replace=true clears the whole track first. Notes past the song end are skipped.",
    input_schema: obj(
      {
        track: trackProp,
        replace: { type: "boolean" },
        notes: {
          type: "array",
          items: obj(
            {
              pitch: { type: "integer", description: "MIDI note 0-127" },
              start: { type: "number", description: "Start in beats from song start" },
              length: { type: "number", description: "Duration in beats" },
              velocity: { type: "integer", description: "1-127" },
            },
            ["pitch", "start", "length", "velocity"]
          ),
        },
      },
      ["track", "notes"]
    ),
  },
  {
    name: "remove_notes",
    description: "Remove notes whose start is in [start_beat, end_beat) and pitch in [min_pitch, max_pitch]. Omit the range to clear the track.",
    input_schema: obj(
      {
        track: trackProp,
        start_beat: { type: "number" },
        end_beat: { type: "number" },
        min_pitch: { type: "integer" },
        max_pitch: { type: "integer" },
      },
      ["track"]
    ),
  },
  {
    name: "set_tempo",
    description: "Set the song tempo in BPM (40-300).",
    input_schema: obj({ bpm: { type: "number" } }, ["bpm"]),
  },
  {
    name: "set_song_length",
    description: "Set the song length in bars (1-256). The song loops over this length.",
    input_schema: obj({ bars: { type: "integer" } }, ["bars"]),
  },
  {
    name: "list_instruments",
    description: "List available instruments: built-in synths/sampler and the user's scanned plug-ins.",
    input_schema: obj({}),
  },
  {
    name: "set_instrument",
    description: "Replace a track's instrument.",
    input_schema: obj({ track: trackProp, instrument: { type: "string" } }, ["track", "instrument"]),
  },
  {
    name: "list_effects",
    description: "List available effects: built-in (Reverb, Delay, EQ, Compressor, Chorus, Phaser, Filter, Pitch Shift) and scanned plug-ins.",
    input_schema: obj({}),
  },
  {
    name: "add_effect",
    description: "Append an effect to a track's chain (before the track fader), or to the master bus with track -1. Optional params as {param id or name: value}.",
    input_schema: obj({ track: trackProp, effect: { type: "string" }, params: { type: "object", additionalProperties: { type: "number" } } }, ["track", "effect"]),
  },
  {
    name: "remove_effect",
    description: "Remove an effect from a track's chain by its index.",
    input_schema: obj({ track: trackProp, device: { type: "integer" } }, ["track", "device"]),
  },
  {
    name: "get_device_params",
    description: "List a device's parameters with ranges and current values. Use filter to search large plug-ins.",
    input_schema: obj({ track: trackProp, device: deviceProp, filter: { type: "string" }, limit: { type: "integer" } }, ["track", "device"]),
  },
  {
    name: "set_device_params",
    description: "Set parameters on a track's instrument or effect. params maps param id or name to a value within its range.",
    input_schema: obj({ track: trackProp, device: deviceProp, params: { type: "object", additionalProperties: { type: "number" } } }, ["track", "device", "params"]),
  },
  {
    name: "set_automation",
    description: "Draw an automation envelope (breakpoints) on a parameter. Replaces existing points in the span covered. device: \"mixer\" (param \"volume\" in dB or \"pan\" -1..1), \"instrument\", or an effect index. track -1 = master bus (mixer only).",
    input_schema: obj(
      {
        track: { type: "integer", description: "Track index, or -1 for the master bus" },
        device: deviceOrMixerProp,
        param: { type: "string", description: "Param id or name (see get_device_params), or volume/pan for the mixer" },
        points: {
          type: "array",
          items: obj(
            {
              beat: { type: "number" },
              value: { type: "number", description: "In the param's units (dB for mixer volume)" },
              curve: { type: "number", description: "-1..1 bend of the segment to the next point; 0 = straight" },
            },
            ["beat", "value"]
          ),
        },
      },
      ["track", "device", "param", "points"]
    ),
  },
  {
    name: "clear_automation",
    description: "Remove automation from a parameter (optionally only within [start_beat, end_beat]).",
    input_schema: obj(
      { track: { type: "integer" }, device: deviceOrMixerProp, param: { type: "string" }, start_beat: { type: "number" }, end_beat: { type: "number" } },
      ["track", "device", "param"]
    ),
  },
  {
    name: "fade",
    description: "Volume fade in or out between two beats on a track, or the whole mix when track is -1 or omitted.",
    input_schema: obj(
      {
        track: { type: "integer" },
        type: { type: "string", enum: ["in", "out"] },
        start_beat: { type: "number" },
        end_beat: { type: "number" },
        shape: { type: "string", enum: ["smooth", "linear"] },
      },
      ["type", "start_beat", "end_beat"]
    ),
  },
  {
    name: "analyze_audio",
    description: "Analyze an audio file: tempo, key, sections, energy curve, a musical summary, and a spectrogram image. stems=true separates drums/bass/vocals/other (slow, minutes, blocks you meanwhile: prefer separate_stems). midi=true transcribes to notes: the bass and other stems when stems=true, otherwise the whole file (best on a single-instrument stem).",
    input_schema: obj({ path: { type: "string" }, stems: { type: "boolean" }, midi: { type: "boolean" } }, ["path"]),
  },
  {
    name: "separate_stems",
    description: "Split an audio file into drums, bass, vocals and other with Demucs. Runs in its own window for a few minutes while everything else keeps working; it does not wait. Prefer this over analyze_audio stems=true.",
    input_schema: obj({ path: { type: "string" } }, ["path"]),
  },
  {
    name: "get_separated_stems",
    description: "Get the stem files from the last separate_stems run (paths to drums/bass/vocals/other WAVs), e.g. to analyze or transcribe a stem, or add one as an audio clip.",
    input_schema: obj({}),
  },
  {
    name: "render_and_analyze",
    description: "Render the current song to a WAV and analyze it (as analyze_audio), to check the mix.",
    input_schema: obj({}),
  },
  {
    name: "search_samples",
    description: "Search the user's sample library index. All filters optional; query matches file names and paths.",
    input_schema: obj({
      query: { type: "string" },
      category: { type: "string", description: "kick, snare, hat, clap, cymbal, perc, bass, synth, pad, vocal, fx, other" },
      kind: { type: "string", enum: ["one-shot", "loop"] },
      key: { type: "string" },
      max_results: { type: "integer" },
    }),
  },
  {
    name: "import_midi",
    description: "Import a .mid file. With track: merges its notes into that track at start_beat. Without track: creates one new track per MIDI part. The song grows if the file is longer.",
    input_schema: obj({ path: { type: "string" }, track: { type: "integer" }, start_beat: { type: "number" } }, ["path"]),
  },
  {
    name: "find_midi_clips",
    description: "Search the user's MIDI clip library (Documents\\Synthient\\MIDI Clips: drum grooves, basslines, chord progressions, melodies, single chords and arpeggios). All filters optional. query matches name and category words (e.g. 'house', 'bassline', 'arpeggio', 'minor7'). kind: drums or tonal. key: e.g. 'C minor' (clips with a key in their name). bpm: sorts by closeness to that tempo. Each result has path (for import_midi), category, bpm (the tempo it was written at; notes are stored in beats, so it plays at any song tempo), key, bars, notes, lowest/highest (MIDI note numbers) and polyphony.",
    input_schema: obj({
      query: { type: "string" },
      kind: { type: "string", enum: ["drums", "tonal"] },
      key: { type: "string" },
      bpm: { type: "number" },
      max_results: { type: "integer" },
    }),
  },
  {
    name: "save_song",
    description: "Save the song file (.tracktionedit) without any dialog, like the user pressing Save. If the song already has a file it is saved over that file; an untitled song is saved to Documents\\Synthient\\Songs\\<name>.tracktionedit (an existing file there is never overwritten) and keeps saving there afterwards. Give name only for a new file (Save as). Returns the path. Save when the user asks, and when you finish making a new song.",
    input_schema: obj({ name: { type: "string" } }),
  },
  {
    name: "export_song",
    description: "Render the whole song to Documents\\Synthient\\Exports as wav, mp3 (320 kbps), ogg or flac. Returns the file path.",
    input_schema: obj({ format: { type: "string", enum: ["wav", "mp3", "ogg", "flac"] }, name: { type: "string" } }, ["format"]),
  },
  {
    name: "export_stems",
    description: "Render each track to its own file (wav or mp3) in a new folder under Documents\\Synthient\\Exports.",
    input_schema: obj({ format: { type: "string", enum: ["wav", "mp3", "ogg", "flac"] }, name: { type: "string" } }, ["format"]),
  },
  {
    name: "export_midi",
    description: "Write a .mid file of one track (track index) or all tracks (omit track) to Documents\\Synthient\\Exports.",
    input_schema: obj({ track: { type: "integer" }, name: { type: "string" } }),
  },
  {
    name: "lock",
    description: "Lock a whole track, or bars [start_bar, end_bar) on it, so nothing there can change. Only when the user asks.",
    input_schema: obj({ track: trackProp, start_bar: { type: "integer" }, end_bar: { type: "integer" } }, ["track"]),
  },
  {
    name: "unlock",
    description: "Remove locks from a track (all of them, or those overlapping the given bars). Only when the user asks.",
    input_schema: obj({ track: trackProp, start_bar: { type: "integer" }, end_bar: { type: "integer" } }, ["track"]),
  },
  {
    name: "find_instruments",
    description: "Search the sampled instrument library (orchestral, piano, guitars, basses, choir, General MIDI) plus the user's own instruments (source 'User', e.g. converted Ableton Sampler/Simpler presets and Drum Rack kits). Filter by family (strings, woodwinds, brass, keys, guitar, bass, percussion, choir, synth, drums...) and/or a text query (e.g. 'cello', 'pizzicato', 'nylon'; 'user' limits results to the user's own). Returns at most max_results (default 30), built-in instruments first.",
    input_schema: obj({ family: { type: "string" }, query: { type: "string" }, max_results: { type: "integer" } }),
  },
  {
    name: "load_instrument",
    description: "Load a sampled instrument from find_instruments onto a track (replaces the track's instrument).",
    input_schema: obj({ track: trackProp, name: { type: "string", description: "Exact instrument name from find_instruments" } }, ["track", "name"]),
  },
  {
    name: "find_synth_patches",
    description: "Search synth patches (OB-Xf and Surge XT). synth: 'OB-Xf' or 'Surge XT' (optional). category: Bass, Lead, Pad, Pluck, Keys, Polysynth, Brass, Strings, Chords, Sequence, Percussion, FX, Winds, Split, Other. warm=true returns only warm/analog-style patches. query: words matched against name, synth, category, author and comment. Returns up to max_results (default 30).",
    input_schema: obj({ synth: { type: "string" }, category: { type: "string" }, warm: { type: "boolean" }, query: { type: "string" }, max_results: { type: "integer" } }),
  },
  {
    name: "load_synth_patch",
    description: "Load a synth patch from find_synth_patches onto a track (switches the track to that synth if needed).",
    input_schema: obj({ track: trackProp, name: { type: "string", description: "Exact patch name from find_synth_patches" }, synth: { type: "string", description: "The patch's synth, from find_synth_patches" } }, ["track", "name"]),
  },
  {
    name: "list_programs",
    description: "List a plug-in's built-in sounds (e.g. Dexed's DX7 voices) on a track.",
    input_schema: obj({ track: trackProp }, ["track"]),
  },
  {
    name: "set_program",
    description: "Choose one of a plug-in's built-in sounds by index or exact name from list_programs.",
    input_schema: obj({ track: trackProp, program: { anyOf: [{ type: "integer" }, { type: "string" }], description: "Index or name" } }, ["track", "program"]),
  },
  {
    name: "add_clip",
    description: "Create an empty MIDI clip on a track over bars [start_bar, end_bar) (1-based, end exclusive). Must not overlap another clip.",
    input_schema: obj({ track: trackProp, start_bar: { type: "integer" }, end_bar: { type: "integer" }, name: { type: "string" } }, ["track", "start_bar", "end_bar"]),
  },
  {
    name: "move_clip",
    description: "Move a MIDI clip (index from clips in get_song) so it starts at dest_bar, with its notes. copy=true duplicates it there instead.",
    input_schema: obj({ track: trackProp, clip: { type: "integer" }, dest_bar: { type: "integer" }, copy: { type: "boolean" } }, ["track", "clip", "dest_bar"]),
  },
  {
    name: "resize_clip",
    description: "Change a MIDI clip's start and/or end bar (notes keep their song position; notes outside the new range stop playing).",
    input_schema: obj({ track: trackProp, clip: { type: "integer" }, start_bar: { type: "integer" }, end_bar: { type: "integer" } }, ["track", "clip"]),
  },
  {
    name: "delete_clip",
    description: "Delete a MIDI clip and its notes.",
    input_schema: obj({ track: trackProp, clip: { type: "integer" } }, ["track", "clip"]),
  },
  {
    name: "add_bus",
    description: "Create a send/return bus track (optionally with an effect, e.g. Reverb with params). Returns its track index for set_send.",
    input_schema: obj({ name: { type: "string" }, effect: { type: "string" }, params: { type: "object", additionalProperties: { type: "number" } } }, ["name"]),
  },
  {
    name: "set_send",
    description: "Send a track to a bus at level_db (post-fader). level_db <= -60 removes the send.",
    input_schema: obj({ track: trackProp, bus_track: { type: "integer" }, level_db: { type: "number" } }, ["track", "bus_track", "level_db"]),
  },
  {
    name: "sidechain_duck",
    description: "Duck a track under another (e.g. bass under kick): adds a sidechained compressor. amount 0..1 (default 0.7), release_ms (default 150).",
    input_schema: obj({ track: trackProp, source_track: { type: "integer" }, amount: { type: "number" }, release_ms: { type: "number" } }, ["track", "source_track"]),
  },
  {
    name: "set_sidechain",
    description: "Set a device's sidechain input to another track (source_track -1 clears).",
    input_schema: obj({ track: trackProp, device: deviceProp, source_track: { type: "integer" } }, ["track", "device", "source_track"]),
  },
  {
    name: "add_audio_clip",
    description: "Place an audio file on a track. With source_bpm the clip follows the song tempo; loop=true repeats it to fill length_beats.",
    input_schema: obj(
      {
        track: trackProp,
        path: { type: "string" },
        start_beat: { type: "number" },
        length_beats: { type: "number" },
        source_bpm: { type: "number" },
        loop: { type: "boolean" },
        gain_db: { type: "number" },
      },
      ["track", "path"]
    ),
  },
  {
    name: "remove_audio_clip",
    description: "Remove an audio clip (index from audioClips in get_song).",
    input_schema: obj({ track: trackProp, clip: { type: "integer" } }, ["track", "clip"]),
  },
  {
    name: "set_sections",
    description: "Label the song structure. Replaces all sections. Bars are 1-based; end_bar is exclusive.",
    input_schema: obj({
      sections: { type: "array", items: obj({ name: { type: "string" }, start_bar: { type: "integer" }, end_bar: { type: "integer" } }, ["name", "start_bar", "end_bar"]) },
    }, ["sections"]),
  },
  {
    name: "copy_bars",
    description: "Copy notes from bars [from_bar, to_bar) to start at dest_bar, on the given tracks (default all). Replaces notes in the destination unless merge=true. Optional transpose in semitones.",
    input_schema: obj(
      {
        from_bar: { type: "integer" },
        to_bar: { type: "integer" },
        dest_bar: { type: "integer" },
        tracks: { type: "array", items: { type: "integer" } },
        transpose: { type: "integer" },
        merge: { type: "boolean" },
      },
      ["from_bar", "to_bar", "dest_bar"]
    ),
  },
  {
    name: "clear_bars",
    description: "Remove notes in bars [from_bar, to_bar) on the given tracks (default all).",
    input_schema: obj({ from_bar: { type: "integer" }, to_bar: { type: "integer" }, tracks: { type: "array", items: { type: "integer" } } }, ["from_bar", "to_bar"]),
  },
  {
    name: "groove",
    description: "Change the feel of a track's notes in a beat range: quantize (grid in beats), swing 0..1 on swing_grid (0.5 = 8ths, 0.25 = 16ths), humanize_timing (max beats, e.g. 0.02), humanize_velocity 0..1.",
    input_schema: obj(
      {
        track: trackProp,
        start_beat: { type: "number" },
        end_beat: { type: "number" },
        quantize: { type: "number" },
        swing: { type: "number" },
        swing_grid: { type: "number" },
        humanize_timing: { type: "number" },
        humanize_velocity: { type: "number" },
      },
      ["track"]
    ),
  },
  {
    name: "transpose",
    description: "Shift notes by semitones on given tracks (default all) within an optional beat range.",
    input_schema: obj({ semitones: { type: "integer" }, tracks: { type: "array", items: { type: "integer" } }, start_beat: { type: "number" }, end_beat: { type: "number" } }, ["semitones"]),
  },
  {
    name: "set_key",
    description: "Set the song key, e.g. root A scale minor. Scales: major, minor, dorian, phrygian, lydian, mixolydian, harmonic minor, minor pentatonic, major pentatonic, blues.",
    input_schema: obj({ root: { type: "string" }, scale: { type: "string" } }, ["root", "scale"]),
  },
  {
    name: "save_take",
    description: "Store a take (variation) on a track. With notes: stores those notes and (by default) makes the take active. Without notes: saves the track's current notes under name.",
    input_schema: obj(
      {
        track: trackProp,
        name: { type: "string" },
        notes: { type: "array", items: obj({ pitch: { type: "integer" }, start: { type: "number" }, length: { type: "number" }, velocity: { type: "integer" } }, ["pitch", "start", "length", "velocity"]) },
        activate: { type: "boolean" },
      },
      ["track", "name"]
    ),
  },
  {
    name: "use_take",
    description: "Switch a track to a stored take (the current notes are kept under the previously active take).",
    input_schema: obj({ track: trackProp, name: { type: "string" } }, ["track", "name"]),
  },
  {
    name: "load_sample",
    description: "Load an audio file into a track's Sampler (switching the track to Sampler if needed), mapped to a key range.",
    input_schema: obj(
      {
        track: trackProp,
        path: { type: "string", description: "Absolute path to a wav/aif/flac/mp3/ogg file" },
        root_note: { type: "integer", description: "Key that plays the sample at original pitch (default 60)" },
        min_note: { type: "integer" },
        max_note: { type: "integer" },
      },
      ["track", "path"]
    ),
  },
];

// Runs one tool the way the chat does: UI-side tools first, the rest in the engine.
// Returns { result, content? } (content: a tool_result content array, e.g. text + an image).
// Also used by the Claude Code bridge (app.js), so both get identical behaviour.
export async function runToolCall(native, localTools, name, input) {
  const local = localTools[name];
  return local ? await local(input ?? {}) : { result: await native.runTool(name, input ?? {}) };
}

export class ClaudeSession {
  constructor(native, { onText, onTool, onError, onChange, onUsage = () => {}, localTools = {}, extraTools = [], getSystemExtra = () => "" }) {
    this.native = native;
    this.model = "claude-opus-5-5";
    this.onUsage = onUsage;
    this.localTools = localTools;
    this.extraTools = extraTools;
    this.getSystemExtra = getSystemExtra;
    this.callbacks = { onText, onTool, onError, onChange };
    // Append-only history. Assistant turns are stored exactly as returned
    // (including thinking blocks) so they replay unchanged.
    this.messages = [];
    // The system prompt must stay identical for the whole conversation: Claude's thinking blocks
    // are bound to it, and a changed system prompt makes the API reject them. So the producer
    // memory and mode are captured once per conversation; later changes go in as user-turn notes.
    this.systemSnapshot = null;
    this.lastAnnounced = null;
  }

  async request() {
    const body = {
      model: this.model,
      // Cache the repeated prefix (tools, instructions, memory, earlier turns); re-reads cost 5% of input.
      cache_control: { type: "ephemeral" },
      max_tokens: 16000,
      output_config: { effort: "medium" },
      fallbacks: "default",
      // Fixed prompt first (cacheable), then the producer memory and mode.
      system: [
        { type: "text", text: SYSTEM_PROMPT },
        { type: "text", text: (this.systemSnapshot ??= this.getSystemExtra()) },
      ],
      tools: [...TOOLS, ...this.extraTools],
      messages: this.messages,
    };
    let resp;
    try {
      resp = JSON.parse(await this.native.claudeRequest(JSON.stringify(body)));
    } catch (err) {
      return { type: "error", error: { message: `Could not read the API response (${err})` } };
    }
    if (resp?.usage) this.onUsage(resp.usage, resp.model ?? this.model);
    return resp;
  }

  // Start a fresh conversation (the song itself is unaffected).
  reset() {
    this.messages = [];
    this.systemSnapshot = null;
    this.lastAnnounced = null;
  }

  // Last resort if the API still rejects old thinking blocks: drop them from earlier turns
  // (allowed; only the turn in progress needs its thinking) and, if needed, from all turns.
  dropThinking(all) {
    const lastAssistant = this.messages.findLastIndex((m) => m.role === "assistant");
    let dropped = 0;
    this.messages.forEach((m, i) => {
      if (m.role !== "assistant" || !Array.isArray(m.content) || (!all && i === lastAssistant)) return;
      const kept = m.content.filter((b) => b.type !== "thinking" && b.type !== "redacted_thinking");
      dropped += m.content.length - kept.length;
      m.content = kept.length ? kept : [{ type: "text", text: "(continued)" }];
    });
    return dropped;
  }

  // Ask the loop to stop after the current API call.
  cancel() {
    this.cancelled = true;
  }

  // content: a string, or an array of content blocks (text, image).
  async send(content) {
    const { onText, onTool, onError, onChange } = this.callbacks;
    let blocks = typeof content === "string" ? [{ type: "text", text: content }] : content;
    this.cancelled = false;

    // Memory or Auto-mix changed since the conversation started: tell Claude in this turn
    // instead of changing the system prompt (see systemSnapshot).
    const announcedBefore = this.lastAnnounced;
    if (this.systemSnapshot !== null) {
      const current = this.getSystemExtra();
      if (current !== (this.lastAnnounced ?? this.systemSnapshot)) {
        this.lastAnnounced = current;
        blocks = [{ type: "text", text: `[Settings changed during this conversation. These replace the producer memory and mode in your instructions:\n${current}]` }, ...blocks];
      }
    }

    // A stopped or failed loop can leave the history ending in a user turn (tool results);
    // add to that turn rather than starting a second consecutive user turn.
    const last = this.messages.at(-1);
    let undoAdd;
    if (last?.role === "user") {
      if (typeof last.content === "string") last.content = [{ type: "text", text: last.content }];
      const before = last.content.length;
      last.content.push(...blocks);
      undoAdd = () => last.content.splice(before);
    } else {
      this.messages.push({ role: "user", content: blocks });
      undoAdd = () => this.messages.pop();
    }
    let gotReply = false;

    let thinkingRetries = 0;
    for (;;) {
      let resp = await this.request();

      // "Invalid signature in thinking block ... bound to a different conversation": recover by
      // dropping old thinking blocks instead of failing the request.
      while (resp.type === "error" && /thinking/i.test(resp.error?.message ?? "") && /signature|bound/i.test(resp.error?.message ?? "") && thinkingRetries < 2) {
        const dropped = this.dropThinking(thinkingRetries === 1);
        thinkingRetries++;
        if (!dropped) break;
        this.native.logEvent?.(`thinking blocks rejected; dropped ${dropped} and retried`);
        resp = await this.request();
      }

      if (resp.type === "error" || resp.stop_reason === "refusal") {
        onError(resp.type === "error"
          ? `Claude API error: ${resp.error?.message ?? JSON.stringify(resp.error)}`
          : "Claude declined this request. Try rephrasing it.");
        // Nothing was answered yet: take the request back out so the chat stays usable.
        if (!gotReply) { undoAdd(); this.lastAnnounced = announcedBefore; }
        return;
      }
      gotReply = true;

      this.messages.push({ role: "assistant", content: resp.content });

      for (const block of resp.content) {
        if (block.type === "text" && block.text.trim()) onText(block.text);
      }

      const toolUses = resp.content.filter((b) => b.type === "tool_use");
      if (toolUses.length === 0) {
        if (resp.stop_reason === "max_tokens") onError("Response was cut off (max_tokens).");
        return;
      }

      // All tool results go back in a single user message.
      const results = [];
      for (const use of toolUses) {
        if (resp.stop_reason === "max_tokens") {
          results.push({ type: "tool_result", tool_use_id: use.id, content: "Tool call was truncated; not run.", is_error: true });
          continue;
        }
        // Local tools may return { result, content } where content is a full
        // tool_result content array (e.g. text + an image).
        const out = await runToolCall(this.native, this.localTools, use.name, use.input);
        onTool(use, out.result);
        results.push({
          type: "tool_result",
          tool_use_id: use.id,
          content: out.content ?? JSON.stringify(out.result),
          is_error: out.result?.ok === false,
        });
      }
      this.messages.push({ role: "user", content: results });
      await onChange();

      if (this.cancelled) {
        onError("Stopped. Changes made so far are kept (undo with Ctrl+Z).");
        return;
      }
    }
  }
}
