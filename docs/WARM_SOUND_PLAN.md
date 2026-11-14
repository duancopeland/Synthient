# Plan: warmer, less "computery" sound (approved 2026-11-03)

The user (synthwave, spacy downtempo) finds Surge XT patches too MIDI/digital. All three parts approved.

## Part 1: use what we have better (no downloads) - lead session
- Generic `load_synth_patch` for JUCE plug-ins whose .fxp is an FPCh chunk: payload after the 60-byte
  header goes into the VST3 `IComponent` state. Surge XT (fxID `cjs3`, payload `sub3...`) already works;
  OB-Xf (fxID `OBXf`, payload `VC2!` + XML, 488 patches in `library/plugins/OB-Xf-assets/.../Patches/<Category>`).
  Index OB-Xf patches alongside Surge's (`library/synth_patches.json` or extend `surge_patches.json` with a `synth` field).
- Dexed (DX7): choose its built-in programs via the hosted instance's `setCurrentProgram` (32 ROM voices).
- Warm/analog defaults: producer memory rules + system prompt guidance (drift/detune, tape-style saturation,
  Juno chorus, filtered delays, big reverbs, high-cut, humanized timing/velocity, legato overlaps).
- Curated shortlist of warm synthwave/downtempo patches (tagged in the index, e.g. `"tags": ["warm", "synthwave"]`).
- Patch preview button in the Instrument & effects tab (plays a short chord on the selected track).

## Part 2: free add-ons (downloads need explicit user OK with file, source, size)
- Odin 2, Chow Tape Model (open source: can ship in the installer).
- TAL-NoiseMaker, TAL-Chorus-LX, TAL-Reverb-4, Valhalla Supermassive (free, this PC only unless licenses allow).
- Research notes: `docs/ADDON_DOWNLOADS.md` (written by a helper).

## Part 3: Decent Sampler / Pianobook support
- Converter `.dspreset` (XML: groups/samples, loNote/hiNote/rootNote, loVel/hiVel, seqPosition, ADSR, loops)
  to SFZ for our engine, plus a user-instruments folder that gets indexed. Helper writes
  `library/tools/dspreset_to_sfz.py`; notes in `docs/DECENT_SAMPLER_NOTES.md`.

## Status
- [x] Part 1 (2026-11-03): OB-Xf patch loading (patches rewrapped into <program> "single-program-format"), obxf_patches.json,
  Dexed list_programs/set_program, ▶ Preview (live MIDI injection), warm tags + warmth guidance in the system prompt,
  new songs default to OB-Xf (Retrowaver / OB One / Smother Lead), bundled plug-ins scanned before the first song.
- [x] Part 2 (2026-11-03, user approved): installed on this PC: CHOWTapeModel 2.11.4, Odin2 2.4.1 (dev build, unsigned,
  official GitHub), ValhallaSupermassive 5.0.0 (Common Files\VST3, via their Inno installers), TAL-NoiseMaker,
  TAL-Chorus-LX, TAL Reverb 4 (VST3 bundles copied into the app's instruments\ folder, not redistributable).
  Odin2 + CHOWTapeModel (GPLv3) copied into library/plugins with licenses so the full installer ships them.
- [x] Part 3 (2026-11-03): analysis/dspreset_to_sfz.py + index_user_instruments.py (cli tools), Documents\Synthient\Instruments,
  "Your instruments…" / Rescan buttons, menu group "Your instruments", Claude sees them as source "User".
- [x] Part 4 (2026-11-03): Claude Code bridge. Source/Bridge.cpp (127.0.0.1, random port + token in bridge.json,
  GET /tools, POST /call) -> UI handleBridgeCall -> runToolCall (same dispatch as the chat) -> bridgeReply.
  analysis/synthient_mcp.py (stdlib MCP stdio server; instructions = system prompt + producer memory; spectrograms as
  image content; prompt "compose"). Extra tools get_instructions, get_selection. Chat shows "Claude Code: ..." lines.
  Setup: docs/CLAUDE_CODE.md (shipped as CLAUDE_CODE.md next to the exe). Tested end to end on an isolated test copy.
- Testing: scratchpad runtest.ps1 runs an isolated test copy (SYNTHIENT_DATA_DIR + own WebView profile).

## Part 4 (requested 2026-11-03): let Claude Code drive Synthient
- Synthient runs a local-only HTTP endpoint (127.0.0.1, random port written to %APPDATA%\Synthient\bridge.json
  with a per-session token). It forwards tool calls into the web UI (same tools as the in-app chat,
  including JS-side ones like find_instruments) and returns results.
- A stdio MCP server script (Python, uses the bundled analysis Python, no SDK needed) that Claude Code
  launches: lists Synthient's tools and proxies calls. Setup doc + `claude mcp add` command.
- Claude Code then composes on the user's Claude plan instead of API credits.
