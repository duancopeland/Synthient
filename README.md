# Synthient

[![Discord](https://img.shields.io/badge/Discord-join%20the%20community-5865F2?logo=discord&logoColor=white)](https://discord.gg/82p23Fguj2)
[![Latest release](https://img.shields.io/github/v/release/guyverphd/Synthient)](https://github.com/guyverphd/Synthient/releases/latest)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0-blue)](LICENSE)

**A real Windows DAW with Claude built in.** Write, edit and mix songs yourself with a piano roll,
drum grid, VST3 instruments and effects, automation and mixing, or have Claude work alongside
you: describe a part in plain words and watch it appear on the tracks, then grab it and change it
by hand. Every change Claude makes is a normal edit, so you can see it, move it and undo it.

| [![Claude Made This Song: Uplifting Video Game Music with a Sax Hook](https://img.youtube.com/vi/QAS_1Bj19Xg/maxresdefault.jpg)](https://youtu.be/QAS_1Bj19Xg) | [![Claude Made This Funk Track: Slap Bass, Rhodes and Brass](https://img.youtube.com/vi/UNV-MSRU9-Y/maxresdefault.jpg)](https://youtu.be/UNV-MSRU9-Y) |
|:---:|:---:|
| ▶ **[Uplifting video game music with a sax hook](https://youtu.be/QAS_1Bj19Xg)** | ▶ **[A funk track: slap bass, Rhodes and brass](https://youtu.be/UNV-MSRU9-Y)** |

Both songs were made by Claude inside Synthient.

Open source under the GNU AGPL v3 (see [LICENSE](LICENSE)), as JUCE (AGPLv3) and Tracktion Engine
(GPLv3 or later) require.

- [Two ways to work](#two-ways-to-work)
- [Claude in the app vs Claude Code](#claude-in-the-app-vs-claude-code)
- [Features](#features)
- [A tour of the screen](#a-tour-of-the-screen)
- [What Claude can do](#what-claude-can-do)
- [Shortcuts](#shortcuts)
- [Install](#install)
- [Build from source](#build-from-source)
- [How it works](#how-it-works)
- [Community](#community)

## Two ways to work

**Use it as a DAW.** Synthient is a full multitrack workstation that doesn't need Claude at all:
- Draw notes in the piano roll, program beats in the drum grid, and drag clips around the timeline.
- Load synths, sampled instruments and VST3 plug-ins, and stack effects.
- Automate any parameter, mix with buses and sidechain, and export the song, its stems or MIDI.

**Or work in tandem with Claude.** Ask for a whole song, one part, or a change to the bars you've
selected:
- Claude writes the notes, picks and shapes the sounds, arranges sections and mixes.
- It can render the mix and listen to its own work, then fix what's too loud or muddy.
- Everything it does lands on the same tracks you're working on, in real time:
  - Highlight bars and ask for "busier", "simpler" or "three variations".
  - Drag a clip Claude wrote somewhere else, change its instrument, or lock a part so Claude never touches it.
  - Press Ctrl+Z to undo anything Claude did.

## Claude in the app vs Claude Code

Claude can drive Synthient in two ways. Both use the same tools and see the same song.

| | **In-app chat** | **Claude Code** |
|---|---|---|
| Where you type | The chat panel on the right of Synthient | A Claude Code session (terminal, desktop app, IDE) |
| Billing | Your Anthropic API key, pay per request. A cost meter under the chat shows what each request cost | Your Claude subscription (Pro/Max). No API credits, no cost meter |
| Models | Pick in the chat panel: **Opus 5.5** (best results) or **Sonnet 5.5** (half the price) | Whatever model your Claude Code session uses |
| Setup | Set `ANTHROPIC_API_KEY` (the installer asks for it) | One `claude mcp add` command ([docs/CLAUDE_CODE.md](docs/CLAUDE_CODE.md)) |
| Good for | Quick requests while you work, the one-click buttons (Variations, Simplify, Busier...) | Long sessions on a subscription, scripting, combining Synthient with other work |

**Claude Code setup (once):**

```
claude mcp add --scope user synthient -- "%LOCALAPPDATA%\Programs\Synthient\analysis\python\python.exe" "%LOCALAPPDATA%\Programs\Synthient\analysis\synthient_mcp.py"
```

Then start Synthient, open a Claude Code session, and ask: *"In Synthient, make a drum & bass
track at 174 BPM, 64 bars."*

A fresh session needs no explanation. When it connects, Synthient hands Claude Code its composing
guide and your producer memory. Each change shows up in Synthient's chat as a "Claude Code: ..." line
and on screen right away.

Only one driver works at a time: while the in-app chat is busy, Claude Code waits.

## Features

### Arrangement and editing
- **Tracks:** any number. Add, remove, rename (double-click), mute, solo and set the volume of each.
- **Clips:** several MIDI clips per track.
  - Drag a clip's top strip to move it, Ctrl-drag to copy, or drag its edges to resize.
  - Double-click empty space to make a clip; right-click a clip to delete it.
- **Piano roll:**
  - Click to add a note, drag to move or resize, right-click or Delete to remove.
  - Shift-click to select several notes, Ctrl+A to select all, Ctrl+wheel for velocity, hold Alt to turn off snap.
- **Drum grid** for Sampler tracks: click to place a hit, Shift-click for an accent.
- **Key and scale:** pick from 10 scales (major, minor, the modes, pentatonics, blues). The piano roll highlights the scale, and **lock** snaps notes to it.
- **Song setup:** set the tempo and song length; play, loop, undo and redo.
- **Time readout:** shows the bar and beat, plus the current time and song length.
- **Timeline zoom:** the − / Fit / + buttons, or Ctrl + mouse wheel.
- **Arrangement tools:**
  - Section labels on the ruler.
  - Copy or clear bars across all tracks.
  - Transpose.
  - Groove: quantize, swing and humanize.
  - **Takes:** keep several versions of a part and switch between them.
- **Drag and drop:**
  - Audio files (wav, mp3, ogg, flac, aif) or MIDI files onto a track at a bar, or onto empty space to make a new track.
  - Audio clips follow the tempo (SoundTouch).

### Working with Claude on a selection
- **Drag across a track** to select bars, and **Ctrl+drag** to add more ranges, even on other tracks. Your next message applies only to the selection.
- The **selection bar** has one-click actions:

| Button | What it does |
|---|---|
| **Variations ×3** | Writes three versions of the part as takes (also the V key) |
| **Simplify** | Removes notes and keeps the idea |
| **Busier** | Adds energy and movement |
| **Continue** | Carries the idea into the next bars |
| **Humanize** | Loosens timing and velocity |
| **Duplicate →** | Copies the selection right after itself (also Ctrl+D) |
| **Clear** | Removes the notes in the selection |
| **🔒 Lock / Unlock** | Protects bars or a whole track so Claude never changes them |

### Sounds
- **OB-Xf** (Oberheim-style analog, ~490 patches) is the default synth: warm pads, brass, basses and leads.
- **Surge XT:** ~2,900 more modern sounds.
- **Dexed:** a DX7 for 80s electric pianos and bells.
- **Sampled instruments** played by Synthient's own SFZ engine:
  - 200+ instruments: VSCO 2 orchestra, Salamander Grand Piano, Karoryfer upright bass and electric guitar, and all 128 General MIDI sounds.
  - Velocity layers, round robins, release samples, sustain pedal and choke groups.
- **Your own sounds:**
  - Any VST3 instrument or effect: click **Plug-ins** to scan for them.
  - SFZ / Decent Sampler instruments you drop in `Documents\Synthient\Instruments`.
  - Converted Ableton Sampler / Drum Rack presets and MIDI clips (`analysis/ableton_to_sfz.py`).
- **Built-in effects:** reverb, delay, EQ, compressor, chorus, phaser, filter and pitch shift.
- **▶ Preview sound** plays a patch before you write anything with it.

### Mixing
- Master bus effects, send/return buses, and sidechain ducking.
- Automation envelopes on any parameter: volume, pan, instrument and effect controls. Fades on tracks or the master.
- **Auto-mix** (a toggle in the chat panel, on by default): Claude applies your mixing rules as it works, such as sidechain, buses, EQ space and levels.
- **Producer memory:** rules and preferences Claude reads before every request. Edit it in the app, or tell Claude "remember I like big gated snares".

### Audio in and out
- **Upload track...** lets Claude analyse a song: tempo, key, sections, energy and a spectrogram. Tick **MIDI** to also transcribe it to notes.
- **Separate stems...** splits a song into drums, bass, vocals and other (Demucs), in its own window, so you keep working. Then add the stems as tracks.
- **Sample folder...** indexes your own samples so Claude can search and use them.
- **Export:** the song as WAV, MP3 (320 kbps), OGG or FLAC, stems as WAV or MP3, or MIDI (all tracks or the selected one).
- **Safety:**
  - Autosave every 2 minutes and crash recovery on the next start.
  - A crash report dialog that can open a GitHub issue for you.

## A tour of the screen

**Top bar**
- **New / Open / Save / Save as.** Songs are `.tracktionedit` files.
- **Export...:** the song, stems or MIDI.
- **▶ / ■:** play/pause. Press ■ a second time to go back to the start.
- **Position and time:** bar.beat, then current time / song length.
- **↶ ↷:** undo and redo.
- **BPM** and **Bars**.
- **Key**, scale, and **lock** (snap notes to the key).
- **Plug-ins:** scan and manage VST3 plug-ins.
- **Audio:** device settings.
- **?:** shortcuts and the tutorial.

**Arrangement:** the timeline with tracks and clips, the selection bar, + Add track, and zoom.

**Bottom panel:**
- **Piano roll** for notes.
- **Drum grid** for beats.
- **Instrument & effects:** the sound, its parameters, effects and plug-in windows.

**Chat panel**

| Control | What it does |
|---|---|
| Example prompts | Click one to put it in the box, edit, then Send |
| **Upload track...** / **MIDI** | Analyse an audio file (and transcribe it to MIDI) |
| **Separate stems...** | Demucs stem separation in its own window |
| **Sample folder...** | Index your samples for Claude |
| **Auto-mix** | Claude applies your mix rules automatically |
| **Producer memory...** | View and edit what Claude knows about your taste |
| Model | Opus 5.5 (best) or Sonnet 5.5 (half price) |
| **Reset chat** | Start a fresh conversation (cheaper; the song is kept) |
| **Stop** | Stop Claude after its current step |
| **chime** | A sound when Claude finishes |
| Cost meter | What each request cost (in-app chat only) |

## What Claude can do

Claude works through the same actions you do. Its toolbox:

- **Song and tracks:**
  - Read the song.
  - Add, remove and set up tracks.
  - Set the tempo, length and key.
  - Add sections.
- **Notes and clips:**
  - Add and remove notes.
  - Add, move, resize and delete clips.
  - Copy and clear bars, transpose, and groove.
  - Lock and unlock parts.
  - Save and switch takes.
  - Import MIDI and find MIDI clips.
- **Sounds:**
  - Find and load instruments, sampled instruments and synth patches.
  - Read and set any device parameter.
  - Pick programs.
  - Search and load samples.
- **Effects and mixing:**
  - Add and remove effects.
  - Write and clear automation, and fades.
  - Buses, sends and sidechain ducking.
- **Listening:**
  - Analyse audio files.
  - Separate stems.
  - **Render the mix and analyse it**, so Claude can hear its own work and fix it.
- **Output:** export the song, stems and MIDI.
- **Memory:** remember your preferences in producer memory.

## Shortcuts

| Keys | Action |
|---|---|
| Drag on a track / Ctrl + drag | Select bars / add another selection |
| Esc | Clear selections |
| V | 3 variations of the selection, as takes |
| Ctrl + D | Duplicate the selection after itself |
| Delete | Clear selected notes or bars |
| L | Lock / unlock the selection (or the track) |
| M / S | Mute / solo the selected track |
| Space | Play / pause |
| Ctrl + Z / Ctrl + Y | Undo / redo (including Claude's changes) |
| Ctrl + S / Ctrl + O | Save / open |
| Ctrl + mouse wheel | Zoom the timeline |

## Install

Windows 10/11, 64-bit. No admin rights needed.

1. Download `Synthient-<version>-win64.zip` (about 400 MB) from the
   [Releases page](https://github.com/guyverphd/Synthient/releases) and unzip it.
2. Double-click **Install Synthient.cmd**. It:
   - asks where to install (default `%LOCALAPPDATA%\Programs\Synthient`);
   - installs the app and the audio analysis and stem tools;
   - offers to download the instrument library from each project's own release (Surge XT, Dexed, OB-Xf, VSCO orchestra, Salamander piano, Karoryfer bass and guitar, General MIDI sounds; about 4 GB);
   - asks for your Anthropic API key, which you can skip if you only use Claude Code.
3. If you skipped the instruments or the download stopped, run **Synthient - Get Instruments**
   from the Start Menu. Parts that are already installed are skipped.

The instrument library is optional: Synthient runs with its Starter Kit drums and any VST3
plug-ins you already have. Details and the manual route: [docs/LIBRARY_SETUP.md](docs/LIBRARY_SETUP.md).

Credit when releasing music made with the piano: *Salamander Grand Piano by Alexander Holm, CC-BY 3.0*.
VSCO 2 CE and the Karoryfer instruments are CC0; GeneralUser GS is free for any use.

## Build from source

| Layer | Choice |
|---|---|
| Audio engine | [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) (JUCE 9) in `external/` |
| App shell | JUCE C++ app hosting a `WebBrowserComponent` (WebView2) |
| UI | Plain HTML/JS in `ui/`, served from the source tree (edit and reload, no rebuild) |
| Claude | Agent loop in `ui/app.js`. Requests go through the native `claudeRequest` function, which adds the API key (`ANTHROPIC_API_KEY`) so it never reaches the UI |
| Analysis | Python sidecar in `analysis/`: librosa (tempo/key/sections/energy), spectrograms, Basic Pitch (audio to MIDI), optional Demucs stems. All local, no API cost |

1. Set `ANTHROPIC_API_KEY` as a Windows user environment variable (only needed for the in-app chat).
2. Clone dependencies into `external/`:
   - `tracktion_engine` with submodules (JUCE's submodule URL must be HTTPS). Tested with Tracktion Engine `a5e4499` and JUCE `7ff644f`.
   - Apply the local JUCE fix: `git -C external/tracktion_engine apply ../../patches/juce-webbrowser-emitEvent.patch`
   - WebView2 SDK NuGet package unpacked to `external/webview2/Microsoft.Web.WebView2`
3. Analysis tools: run `analysis\setup.cmd` (add `--with-stems` for Demucs). See `analysis/README.md`.
4. Optional instrument library: `analysis\.venv\Scripts\python.exe library\tools\get_library.py`
   ([docs/LIBRARY_SETUP.md](docs/LIBRARY_SETUP.md)). Check it with `library\tools\check_library.py`.
5. Build with Visual Studio 2022 and its C++ workload (CMake ships with VS):

```
cmake -S . -B build/app -G "Visual Studio 17 2022" -A x64
cmake --build build/app --config Release --target ClaudeDAW
```

The app is written to `build/app/ClaudeDAW_artefacts/Release/Synthient.exe`. To package it, run
`packaging/package.ps1` (see [packaging/README.md](packaging/README.md)).

## How it works

- **One source of truth.** The Tracktion `Edit` holds the song. The UI and Claude never keep a
  separate copy: every Claude action (`add_notes`, `set_tempo`, ...) runs in the engine through the
  native `runTool` function. That's why Claude's changes appear instantly and are undoable.
- **Claude Code bridge.** `Source/Bridge.cpp` is a local-only HTTP server (127.0.0.1, with a
  per-session token in `bridge.json`). `analysis/synthient_mcp.py` is the MCP server Claude Code starts.
- **Producer memory** lives in `%APPDATA%\Synthient\claude_memory.md`.
- **Diagnostics:**
  - The log is at `%APPDATA%\Synthient\logs\Synthient.log`, with `crash-*.txt/.dmp` and `freeze-*.dmp` (a watchdog writes one after 10 s unresponsive).
  - Open dumps with WinDbg and the matching PDB from `build/app/ClaudeDAW_artefacts/Release`.

### Known limitations
- Undo doesn't cover instrument and effect parameter changes yet.
- Instrument loading runs on the UI thread, up to ~3 s for the biggest libraries.
- The SFZ engine ignores LFOs, pitch and filter envelopes, EQ and legato slides.
- Synthient hosts VST3 only (no VST2).
- Windows only.

## Community

- **[Join the Discord](https://discord.gg/82p23Fguj2)** to share songs you made, ask questions, and suggest features.
- Found a bug? [Open an issue](https://github.com/guyverphd/Synthient/issues). After a crash,
  Synthient offers to make a report you can attach.
