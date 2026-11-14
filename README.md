# Synthient

A standalone Windows DAW with Claude built in: multitrack MIDI/audio, VST3 instruments and effects, and a prompt panel where Claude writes, edits and arranges parts.

Open source under the GNU AGPL v3 (see [LICENSE](LICENSE)), as JUCE (AGPLv3) and Tracktion Engine (GPLv3 or later) require. Third-party plug-ins, SoundFonts and sample libraries are not in this repo; see [docs/ADDON_DOWNLOADS.md](docs/ADDON_DOWNLOADS.md) and [library/NOTES.md](library/NOTES.md).

## Stack

| Layer | Choice |
|---|---|
| Audio engine | [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) (JUCE 9) in `external/` |
| App shell | JUCE C++ app hosting a `WebBrowserComponent` (WebView2) |
| UI | Plain HTML/JS in `ui/`, served from the source tree (edit and reload, no rebuild) |
| Claude | Agent loop in `ui/app.js`; requests go through the native `claudeRequest` function, which adds the API key (`ANTHROPIC_API_KEY`) so it never reaches the UI |
| Analysis | Python sidecar in `analysis/` (see its README): librosa (tempo/key/sections/energy), spectrogram images, Basic Pitch (audio→MIDI), optional Demucs stems; all local, no API cost |

## Core rule

The Tracktion `Edit` is the single source of truth. The UI and Claude never keep a parallel song model; every Claude tool call (`add_notes`, `set_tempo`, ...) runs in the engine through the native `runTool` function and is undoable.

## Current state

- Any number of slim tracks: add, remove, rename (double-click), mute, solo, volume
- Multiple MIDI clips per track: drag a clip's top strip to move it (Ctrl/Alt-drag copies), drag its edges to resize, double-click empty space to create one, right-click to delete. Notes written anywhere land in the covering clip (a new clip fills the gap if needed)
- Per track: one instrument (Surge XT with ~2,900 patches by default, a sampled library instrument, Sampler, or scanned VST3; 4OSC only for old songs or when Surge XT is missing), an effects chain (Reverb, Delay, EQ, Compressor, Chorus, Phaser, Filter, Pitch Shift, or VST3), parameter sliders, plug-in windows
- Piano roll: click/drag to add, drag to move, drag the right edge to resize, right-click or Delete to remove, Ctrl+wheel for velocity, Alt to disable snap
- Automation envelopes on any parameter (mixer volume/pan, instrument, effects), fades on tracks or the master
- Song length and tempo, loop playback, undo/redo, save/open (.tracktionedit)
- Export menu: song as WAV/MP3 (320 kbps)/OGG/FLAC, stems (one file per track), MIDI (all tracks or the selected one). MP3 needs the analysis tools installed
- Drag and drop audio (wav/mp3/ogg/flac/aif) or MIDI files onto a track at a bar, or onto empty space for new tracks
- Autosave every 2 minutes with crash recovery on the next start
- Plug-ins dialog scans VST3 folders (default: Common Files\VST3 and `instruments/` in this repo)
- Claude (chat panel) has tools for all of the above, plus: analyze an audio file (tempo, key, sections, energy, spectrogram, optional MIDI transcription), stem separation in its own window (`Separate stems…`, Demucs; the DAW stays usable, then "Add stems as tracks"), render-and-listen to its own mix, and search the indexed sample library
- Drag across a track to select bars; the selection is sent with your prompt

- Mixing: master bus effects, send/return buses, sidechain ducking, audio clips (tempo-synced loops via SoundTouch)
- Arrangement: section labels on the ruler, copy/clear bars across tracks, transpose, groove (quantize, swing, humanize)
- Key/scale with piano-roll highlight and optional scale lock; drum grid for Sampler tracks; takes (variations) per track
- Producer memory (`%APPDATA%\Synthient\claude_memory.md`): mix rules and user preferences Claude reads every request; Auto-mix applies them automatically

## Claude Code

Claude Code can drive Synthient with the chat's tools on a Claude plan instead of API credits: a local-only
bridge (`Source/Bridge.cpp`, 127.0.0.1 + per-session token in `bridge.json`) forwards calls into the UI, and
`analysis/synthient_mcp.py` is the MCP server Claude Code starts. Setup, examples and limits: `docs/CLAUDE_CODE.md`.

## Instruments

- **Surge XT** is the main synth: `library/surge_patches.json` indexes ~2,900 factory and third-party patches (built by `library/tools/build_surge_index.py`); `load_synth_patch` loads one by writing the .fxp into Surge's VST3 state. New songs start with Surge patches.
- **Built-in 4OSC synth** (legacy, hidden when Surge XT is installed) with 38 presets (`ui/presets/4osc.json`: basses incl. Reese/wobble/acid, leads, Juno-style pads, plucks, keys, FX, synth drums). Waveforms, unison, filter type and an LFO/envelope modulation matrix are exposed as parameters.
- **Starter Kit** drum samples (`samples/`), synthesized, free to use.
- **Instrument library** (`library/`, ~5 GB, not in git): 204 sampled instruments (VSCO 2 CE orchestra, Salamander Grand Piano, Karoryfer Meatbass and Emilyguitar, all 128 General MIDI sounds from GeneralUser GS) played by Synthient's own SFZ engine (`Source/Sampler/`: velocity layers, round-robins incl. random, keyswitches, loops, release samples, sustain pedal, layered chords, choke groups, CC-driven opcodes, low-pass filter; samples are memory-mapped, so a 1.2 GB piano costs little RAM), plus the Surge XT, Dexed, OB-Xf and juicysfplugin VST3 synths. See `library/NOTES.md`. The app finds it next to the exe, or at the path in `%APPDATA%\Synthient\library_path.txt`.
- Credit when releasing music: *Salamander Grand Piano by Alexander Holm, CC-BY 3.0*. VSCO 2 CE and Karoryfer instruments are CC0; GeneralUser GS is free for any use.

## Next

- Undo for instrument/effect parameter changes
- Big native-function results used to freeze the UI (JUCE escaped them with repeated string replaces). Fixed by a local patch in `external/tracktion_engine/modules/juce/modules/juce_gui_extra/misc/juce_WebBrowserComponent.cpp` (`emitEvent`): re-apply it after updating JUCE. Large data still goes through the `library-data/` resource route
- Diagnostics: `%APPDATA%\Synthient\logs\Synthient.log` (tools, Claude requests, errors), `crash-*.txt/.dmp` and `freeze-*.dmp` (watchdog after 10 s unresponsive). Open dumps with WinDbg/cdb and the PDB from `build/app/ClaudeDAW_artefacts/Release` of the same build
- Instrument loading runs on the UI thread (up to ~3 s for the biggest libraries); move it to a background thread
- SFZ engine ignores LFOs, pitch/filter envelopes, EQ and legato slides
- Analysis: tempo detection on dense mixes needs checking against real music

## Install (users)

One package: `packaging/dist/Synthient-0.1.0-win64.zip` (about 5 GB). Unzip it and double-click `Install Synthient.cmd` (per user, no admin). It asks where to install (default `%LOCALAPPDATA%\Programs\Synthient`), checks free space, copies the app, the instrument library and plug-ins, the analysis and stem tools (a self-contained Python with Demucs and its model), OB-Xf patches, makes Start Menu and desktop shortcuts, asks for the Anthropic API key (hidden input, or replace an existing one) and offers to start the app. Build it with `packaging/package.ps1` after building the app (`-AppOnly` for a quick test zip).

## Setup (developers)

1. Set `ANTHROPIC_API_KEY` as a Windows user environment variable.
2. Clone dependencies into `external/`:
   - `tracktion_engine` with submodules (JUCE's submodule URL must be HTTPS). Tested with Tracktion Engine `a5e4499` and JUCE `7ff644f`.
   - Apply the local JUCE fix: `git -C external/tracktion_engine apply ../../patches/juce-webbrowser-emitEvent.patch`
   - WebView2 SDK NuGet package unpacked to `external/webview2/Microsoft.Web.WebView2`
3. Analysis tools: run `analysis\setup.cmd` (add `--with-stems` for Demucs). See `analysis/README.md`. An installed copy without its own tools uses the folder named in `%APPDATA%\Synthient\analysis_path.txt`.
4. Build (Visual Studio 2022 with the C++ workload; CMake ships with VS):

```
cmake -S . -B build/app -G "Visual Studio 17 2022" -A x64
cmake --build build/app --config Release --target ClaudeDAW
```

The app is written to `build/app/ClaudeDAW_artefacts/Release/Synthient.exe`.
