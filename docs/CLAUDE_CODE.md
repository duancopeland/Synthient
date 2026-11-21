# Compose with Claude Code (your Claude plan, no API credits)

Claude Code can drive Synthient with the same tools the chat panel gives Claude: writing notes,
adding tracks, loading OB-Xf / Surge / Dexed sounds and sampled instruments, effects, mixing,
exporting, rendering and listening to the mix, producer memory. Requests made this way run on your
Claude subscription instead of the Anthropic API key, so they don't show up in the cost meter.

Everything stays on this PC: Synthient only accepts connections from this computer (127.0.0.1),
and only from programs that know the secret token it writes to `%APPDATA%\Synthient\bridge.json`
each time it starts (the file is removed when Synthient closes).

## One-time setup

Install the Synthient update first (the connector files below only exist after that). Then run
this once in a terminal (PowerShell or Command Prompt). It tells Claude Code, for all your
projects, how to start Synthient's connector:

```
claude mcp add --scope user synthient -- "%LOCALAPPDATA%\Programs\Synthient\analysis\python\python.exe" "%LOCALAPPDATA%\Programs\Synthient\analysis\synthient_mcp.py"
```

On another PC, use that PC's install folder (in Command Prompt `%LOCALAPPDATA%\Programs\Synthient\...`
works; in PowerShell write `$env:LOCALAPPDATA\Programs\Synthient\...`). Developers can use the source
tree instead: `D:\ClaudeDAW\analysis\.venv\Scripts\python.exe D:\ClaudeDAW\analysis\synthient_mcp.py`
(the script only needs plain Python 3.8+, no packages).

Check it with `claude mcp list` (it should say `synthient ... Connected` while Synthient is open).
To remove it: `claude mcp remove --scope user synthient`.

## Using it

1. Start Synthient first and open (or start) the song.
2. Start Claude Code (in a terminal: `claude`, or the Claude desktop app's Code tab).
   If Claude Code was already open when you started Synthient, type `/mcp` and reconnect `synthient`.
3. Ask for music in plain words. Claude Code reads Synthient's composing guide and your producer
   memory automatically, and you can also type `/mcp__synthient__compose` followed by your request.

Each change appears in Synthient's chat panel as a "Claude Code: ..." line and on screen right away.
Ctrl+Z in Synthient undoes Claude Code's changes like any other.

Example requests:

- "Look at my Synthient song and add a warm OB-Xf pad playing the chords under the bass, 8 bars."
- "I've selected bars 5-8 on the lead. Write 3 variations as takes."
- "Start a synthwave track at 100 BPM in A minor: gated drums, octave bass, Juno pads, an arpeggio. 16 bars."
- "Mix it: buses, sidechain the bass to the kick, then render and listen and fix whatever is too loud."
- "Export the song as an MP3."

## Limits

- Synthient must be running; Claude Code can't start it. If it's closed, tools answer
  "Synthient isn't running".
- One driver at a time: while the in-app chat is working, Claude Code's tool calls are refused
  until the chat finishes.
- Claude Code limits how big a tool result may be. On long songs, ask for the track list first
  (get_song with include_notes=false) or one track at a time. The limit can be raised with the
  `MAX_MCP_OUTPUT_TOKENS` environment variable.
- Very long calls (render_and_analyze, exports of long songs) are bounded by Claude Code's MCP tool
  timeout; if one times out, raise it with the `MCP_TOOL_TIMEOUT` environment variable (milliseconds).
- Claude Code works on whatever song is open in Synthient right now. It saves with `save_song`
  (no dialog): over the song's own file, or for an untitled song to
  `Documents\Synthient\Songs\<name>.tracktionedit`. It saves when you ask and when it finishes a new song.
- Things that open a window or a file dialog (separate stems, plug-in windows) appear in Synthient
  and need you there.
- If two copies of Synthient are open, Claude Code talks to the one started last.

## How it works (for developers)

- `Source/Bridge.cpp`: a small HTTP server on 127.0.0.1 (random port, random token, both in
  `bridge.json` in the data folder; `SYNTHIENT_DATA_DIR` moves it for test copies). `GET /tools`
  and `POST /call {name, input}`, `Authorization: Bearer <token>`. Requests are handled one at a time.
- Each request is passed to the web UI (`bridgeCall` event). `ui/app.js` `handleBridgeCall` runs it
  through `runToolCall` (`ui/claude.js`), the same dispatch the chat uses, then answers with the
  `bridgeReply` native function. Tools: `TOOLS` + `MEMORY_TOOLS` + `get_instructions` (system prompt +
  producer memory + Auto-mix mode) + `get_selection`.
- `analysis/synthient_mcp.py`: MCP over stdio (initialize, tools/list, tools/call, prompts/list,
  prompts/get). Converts tool definitions to MCP (`input_schema` -> `inputSchema`) and results to MCP
  content (spectrogram images become MCP image content). Its `initialize` instructions include the
  full composing guide when Synthient is running.
