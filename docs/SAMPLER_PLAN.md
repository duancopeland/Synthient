# Plan: ClaudeDAW Sampler (SFZ instrument engine)

Approved by the user on 2026-10-30. **Built 2026-10-31** (`Source/Sampler/`, `load_sfz` tool, `library/sfz/GeneralUser-GS`, see `docs/SF2_EXPORT_NOTES.md`); all verification below passed and both installers were rebuilt. Goal: replace "one sample per key" loading (Tracktion SamplerPlugin) with a proper multi-sample instrument so the 200-instrument library sounds realistic.

## Why

`library/index.json` instruments currently load into Tracktion's `SamplerPlugin` via repeated `load_sample` calls (`ui/app.js` `loadLibraryInstrument`). That plugin has: max 64 sounds, one sound per key range, no velocity layers, no round-robins, no loops (samples play once to their end), no release triggers, no simultaneous regions. Consequences the user noticed: flat dynamics, robotic repeats, Meatbass bowed notes stop after ~4 s, Emilyguitar chords/multi-voice programs skipped, GeneralUser GS renders are fixed 3 s loops.

## Design

1. **New internal Tracktion plugin `ClaudeSamplerPlugin`** (Source/Sampler/), registered as a built-in instrument named "Instrument" (list next to 4OSC/Sampler). `isSynth() = true`, `takesMidiInput() = true`.
   - State: the path of an `.sfz` file (or a library instrument id) stored in the plugin ValueTree, so songs save/load and undo work.
   - Engine: own voice allocator (32-64 voices), per-region: `sample`, `lokey/hikey/key`, `pitch_keycenter`, `lovel/hivel`, `seq_length/seq_position` (round-robin), `trigger=attack|release`, `loop_mode` (`no_loop|one_shot|loop_continuous|loop_sustain`), `loop_start/loop_end` (also read the WAV `smpl` chunk when the SFZ doesn't say), `tune/transpose`, `volume/pan`, `amp_veltrack`, `ampeg_attack/decay/sustain/release`, `offset`, `group/off_by` (choke, e.g. hats). `<control> default_path`, `<global>/<master>/<group>` inheritance, `#define`. Reuse the parsing logic already written in `library/tools/` (Python) as the reference.
   - Samples: load via `te::AudioFile` / JUCE `AudioFormatReader` into memory per instrument (cache shared across tracks), with linear or Lagrange resampling for pitch.
   - Exposed automatable params: attack, release, volume, pan, velocity sensitivity, tune.
2. **SF2**: re-extract GeneralUser GS with original loop points and velocity layers into SFZ + WAV (`library/tools/` script), so GM sounds also go through the new engine instead of 3 s renders.
3. **Index**: `library/index.json` entries gain `"sfz": "relative/path.sfz"`; keep `regions` for the old path as a fallback.
4. **Tools/UI**: `load_instrument` switches the track to the new plugin and sets its SFZ (one call instead of 64 `load_sample`). The instrument menu keeps working. Claude-facing tool names unchanged.
5. **OB-Xf portability**: copy its factory data into `library/plugins/OB-Xf-data/`; `packaging/install.ps1` copies it to `%LOCALAPPDATA%\Surge Synth Team\OB-Xf` when present.

## Verification (must pass before shipping)

- Render tests (pattern from the preset test: CDP-driven, export WAV, measure peak): soft vs loud notes differ in timbre/level (velocity layers), repeated notes differ (round-robin), a 10 s held Meatbass note still sounds at 9 s (loops), Emilyguitar chord program sounds several samples at once, release samples fire on note-off.
- Save/load round-trip keeps the instrument; undo works; export with the new plugin doesn't hang (async renderer).
- CPU: 16-voice chords on 4 tracks play without dropouts.
- Rebuild both installers (`packaging/package.ps1` and `-WithLibrary`).

## Working notes for whoever picks this up

- Build: `cmake --build build/app --config Release --target ClaudeDAW` (VS 2022 cmake at `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`).
- Test the app via WebView2 DevTools: start it with env `WEBVIEW2_USER_DATA_FOLDER=<scratch dir>` and `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9333`, drive native functions with `import('./juce.js').then(m => m.getNativeFunction(...))`. Close ONLY the test process by PID - the user may have ClaudeDAW open (closing it once lost their unsaved song).
- Release builds include a PDB; for crashes run under `cdb` with `-y build/app/ClaudeDAW_artefacts/Release`.
- Tracktion audio clips need `TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1` (already set) - missing time-stretch support crashed playback before.
