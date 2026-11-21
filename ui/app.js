import { getNativeFunction } from "./juce.js";
import { ClaudeSession, MODELS, costOf, TOOLS, SYSTEM_PROMPT, runToolCall } from "./claude.js";
import { loadMemory, appendNote, MEMORY_TOOLS } from "./memory.js";

const native = Object.fromEntries(
  [
    "getState", "runTool", "claudeRequest", "play", "stop", "setPosition", "undo", "redo",
    "openDeviceWindow", "scanPlugins", "audioSettings", "chooseSample",
    "newSong", "openSong", "saveSong", "saveSongQuietly", "exportWav", "exportWavTo",
    "analyzeAudio", "scanSampleFolder", "getSampleIndex", "readMemory", "writeMemory",
    "exportAudio", "exportStems", "exportMidi", "defaultExportPath", "importMedia", "getRecovery", "recover", "getTransport", "notifyDone", "getInstrumentIndex", "separateStems", "getSynthPatchIndex", "getLibraryPath", "scanPluginsNow", "logEvent", "previewNotes", "openInstrumentsFolder", "rescanUserInstruments", "getMidiClipIndex", "getCrashReport", "makeCrashReport", "openIssue", "bridgeReady", "bridgeReply",
  ].map((n) => [n, getNativeFunction(n)])
);

const $ = (id) => document.getElementById(id);
const SNAP = 0.25; // beats (1/16 note)

let state = null;          // full song state from the engine
let selectedTrack = 0;
let selections = [];       // [{ track, startBeat, endBeat }] in the arrangement; Ctrl-drag adds more
let activeTab = "roll";
let instrumentsCache = null, effectsCache = null;
let devicesSignature = "";
let renaming = false;
let lastStems = null;        // the most recent stems-window result

// ---------------------------------------------------------------------------
// Engine helpers

let trackSignature = "";

function clearSelections() {
  selections = [];
  rollSelected.clear();
  rollSelKeys = null;
}

async function refresh() {
  state = await native.getState(true);
  // Selections hold track indices; drop them when tracks are added, removed or reordered.
  const sig = state.tracks.map((t) => t.id ?? t.name).join("|");
  if (sig !== trackSignature) {
    if (trackSignature) clearSelections();
    trackSignature = sig;
  }
  if (selectedTrack >= 0) selectedTrack = Math.min(selectedTrack, Math.max(0, state.tracks.length - 1));
  selections = selections.filter((s) => s.track < state.tracks.length && s.endBeat <= songBeats());
  renderAll();
}

async function tool(name, input) {
  if (busy) {
    logMessage("error", "Claude is still working - wait for it, or press Stop.");
    return { ok: false, error: "busy" };
  }
  const result = await native.runTool(name, input);
  if (result?.ok === false) logMessage("error", `${name}: ${result.error}`);
  await refresh();
  return result;
}

async function fileAction(name, ...args) {
  if (busy) return logMessage("error", "Claude is still working - wait for it, or press Stop.");
  const r = await native[name](...args);
  if (r && r.ok === false && r.error !== "cancelled") logMessage("error", r.error);
  if (name === "exportWav" && r?.ok) logMessage("tool", `Exported ${r.value}`);
  if (name === "newSong" || name === "openSong") { clearSelections(); selectedTrack = 0; devicesSignature = ""; claude.reset(); }
  await refresh();
}

const songBeats = () => state.bars * state.beatsPerBar;
const trackName = (i) => state?.tracks?.[i]?.name ?? `track ${i}`;

// ---------------------------------------------------------------------------
// Top bar

function renderTopBar() {
  if (document.activeElement !== $("bpm")) $("bpm").value = Math.round(state.bpm * 100) / 100;
  if (document.activeElement !== $("bars")) $("bars").value = state.bars;
  $("play").textContent = state.playing ? "❚❚" : "▶";
  $("undo").disabled = !state.canUndo;
  $("redo").disabled = !state.canRedo;
  $("file-name").textContent = state.file || "Untitled";
  if (document.activeElement !== $("key-root")) $("key-root").value = state.key?.root ?? "";
  if (document.activeElement !== $("key-scale")) $("key-scale").value = state.key?.scale ?? "minor";
  renderPosition();
}

function renderPosition() {
  const b = state.positionBeats;
  $("position").textContent = `${Math.floor(b / 4) + 1}.${Math.floor(b % 4) + 1}`;
  // Song time: the song has a single tempo, so seconds = beats * 60 / bpm.
  const spb = 60 / (state.bpm || 120);
  $("time").textContent = `${formatTime(b * spb, true)} / ${formatTime(songBeats() * spb, false)}`;
}

function formatTime(sec, tenths) {
  // Round once in the smallest shown unit so 59.96 s reads 1:00.0, not 0:60.0.
  const unit = tenths ? 10 : 1;
  const t = Math.round(Math.max(0, sec) * unit);
  const m = Math.floor(t / (60 * unit)), rest = t - m * 60 * unit;
  const s = tenths ? `${String(Math.floor(rest / 10)).padStart(2, "0")}.${rest % 10}` : String(rest).padStart(2, "0");
  return `${m}:${s}`;
}

// ---------------------------------------------------------------------------
// Arrangement

function renderLanes() {
  if (renaming) return;
  const tracksEl = $("tracks");
  tracksEl.innerHTML = "";

  state.tracks.forEach((t) => {
    const lane = document.createElement("div");
    lane.className = "lane" + (t.index === selectedTrack ? " selected" : "");
    lane.dataset.track = t.index;
    lane.innerHTML = `
      <div class="lane-header" title="Click to select · double-click name to rename">
        <span class="swatch" style="background:${t.colour}"></span>
        <span class="name">${escapeHtml(t.name)}<small>${t.kind === "bus" ? "BUS" : escapeHtml(t.instrument?.name ?? (t.audioClips?.length ? "audio" : "—"))}</small></span>
        ${t.takes ? `<select class="takes" title="Takes (variations)">${t.takes.names.map((n) => `<option ${n === t.takes.active ? "selected" : ""}>${escapeHtml(n)}</option>`).join("")}</select>` : ""}
        <button class="mute ${t.mute ? "on" : ""}" title="Mute">M</button>
        <button class="solo ${t.solo ? "on solo" : ""}" title="Solo">S</button>
        <input type="range" min="-60" max="6" step="0.5" value="${t.volumeDb ?? 0}" title="Volume ${(t.volumeDb ?? 0).toFixed(1)} dB">
        <button class="lock ${t.lock?.locked ? "on" : ""}" title="${t.lock?.locked ? "Locked - click to unlock" : "Lock track (nothing on it can be changed)"}">🔒</button>
        <button class="remove" title="Delete track">✕</button>
      </div>
      <canvas></canvas>`;
    tracksEl.appendChild(lane);

    const header = lane.querySelector(".lane-header");
    header.addEventListener("click", (e) => {
      if (e.target.closest("button, input, select")) return;
      selectTrack(t.index);
    });
    header.querySelector(".name").addEventListener("dblclick", () => startRename(header, t));
    header.querySelector(".mute").onclick = () => tool("set_track", { track: t.index, mute: !t.mute });
    header.querySelector(".lock").onclick = () => tool(t.lock?.locked ? "unlock" : "lock", { track: t.index });
    header.querySelector(".solo").onclick = () => tool("set_track", { track: t.index, solo: !t.solo });
    header.querySelector("input").onchange = (e) => tool("set_track", { track: t.index, volume_db: Number(e.target.value) });
    header.querySelector(".takes")?.addEventListener("change", (e) => tool("use_take", { track: t.index, name: e.target.value }));
    header.querySelector(".remove").onclick = () => {
      if (confirm(`Delete track "${t.name}"?`)) tool("remove_track", { track: t.index });
    };
    attachLaneMouse(lane.querySelector("canvas"), t.index);
  });

  const master = document.createElement("div");
  master.className = "lane master" + (selectedTrack === -1 ? " selected" : "");
  master.innerHTML = `<div class="lane-header" title="Master bus: effects and fades on the whole mix">
      <span class="swatch" style="background:#e4e7ee"></span>
      <span class="name">Master<small>${state.masterEffects.length ? state.masterEffects.map((e) => escapeHtml(e.name)).join(", ") : "no effects"}</small></span>
    </div><canvas></canvas>`;
  master.querySelector(".lane-header").onclick = () => { selectTrack(-1); showTab("devices"); };
  tracksEl.appendChild(master);

  drawLanes();
}

function startRename(header, t) {
  renaming = true;
  const nameEl = header.querySelector(".name");
  const input = document.createElement("input");
  input.className = "rename";
  input.value = t.name;
  nameEl.replaceWith(input);
  input.focus();
  input.select();
  const finish = async (commit) => {
    if (!renaming) return;
    renaming = false;
    if (commit && input.value.trim() && input.value !== t.name) await tool("set_track", { track: t.index, name: input.value.trim() });
    else renderLanes();
  };
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") finish(true);
    if (e.key === "Escape") finish(false);
    e.stopPropagation();
  });
  input.addEventListener("blur", () => finish(true));
}

function selectTrack(i) {
  if (i === selectedTrack) return;
  selectedTrack = i;
  rollSelected.clear();
  devicesSignature = "";
  renderAll();
}

let clipDrag = null; // { track, clip, mode: move|start|end, copy, ghost: {start, end} }

function clipAt(trackIndex, beat) {
  return state.tracks[trackIndex]?.clips?.find((c) => beat >= c.start_beat && beat < c.end_beat);
}

function attachLaneMouse(canvas, trackIndex) {
  let anchor = null;
  let r = null; // captured on mousedown: selecting a track re-renders the lanes
  const beatAt = (e) => Math.max(0, Math.min(songBeats(), ((e.clientX - r.left) / r.width) * songBeats()));
  canvas.addEventListener("contextmenu", async (e) => {
    e.preventDefault();
    const rect = canvas.getBoundingClientRect();
    const c = clipAt(trackIndex, ((e.clientX - rect.left) / rect.width) * songBeats());
    if (c && confirm(`Delete clip at bars ${c.start_beat / 4 + 1}–${c.end_beat / 4}?`)) await tool("delete_clip", { track: trackIndex, clip: c.clip });
  });
  canvas.addEventListener("mousedown", (e) => {
    if (e.button !== 0) return;
    r = canvas.getBoundingClientRect();
    const y = e.clientY - r.top;
    const b = beatAt(e);
    const c = clipAt(trackIndex, b);

    // Clip editing from the clip's top strip: drag to move (Ctrl/Alt copies), edges resize.
    if (c && y <= 10) {
      const px = (beat) => r.left + (beat / songBeats()) * r.width;
      const mode = e.clientX - px(c.start_beat) < 6 ? "start" : px(c.end_beat) - e.clientX < 6 ? "end" : "move";
      clipDrag = { track: trackIndex, clip: c.clip, mode, copy: e.ctrlKey || e.altKey, ghost: { start: c.start_beat, end: c.end_beat } };
      const origin = b;
      const move = (ev) => {
        const delta = Math.round((beatAt(ev) - origin) / 4) * 4;
        const len = c.end_beat - c.start_beat;
        if (mode === "move") {
          const s = Math.max(0, Math.min(songBeats() - len, c.start_beat + delta));
          clipDrag.ghost = { start: s, end: s + len };
        } else if (mode === "start") {
          clipDrag.ghost = { start: Math.max(0, Math.min(c.end_beat - 4, c.start_beat + delta)), end: c.end_beat };
        } else {
          clipDrag.ghost = { start: c.start_beat, end: Math.min(songBeats(), Math.max(c.start_beat + 4, c.end_beat + delta)) };
        }
        drawLanes();
      };
      const up = async () => {
        window.removeEventListener("mousemove", move);
        window.removeEventListener("mouseup", up);
        const d = clipDrag;
        clipDrag = null;
        const g = d.ghost;
        if (mode === "move" && g.start !== c.start_beat) await tool("move_clip", { track: trackIndex, clip: c.clip, dest_bar: g.start / 4 + 1, copy: d.copy });
        else if (mode !== "move" && (g.start !== c.start_beat || g.end !== c.end_beat)) await tool("resize_clip", { track: trackIndex, clip: c.clip, start_bar: g.start / 4 + 1, end_bar: g.end / 4 + 1 });
        else drawLanes();
      };
      window.addEventListener("mousemove", move);
      window.addEventListener("mouseup", up);
      return;
    }
    anchor = Math.max(0, Math.min(Math.floor(beatAt(e) / 4) * 4, songBeats() - 4));
    const adding = e.ctrlKey || e.metaKey;
    selectTrack(trackIndex);
    if (!adding) selections = [];
    const mine = { track: trackIndex, startBeat: anchor, endBeat: Math.min(anchor + 4, songBeats()) };
    selections.push(mine);
    renderSelectionLabel();
    drawLanes();
    const move = (ev) => {
      const b = beatAt(ev);
      mine.startBeat = Math.min(anchor, Math.floor(b / 4) * 4);
      mine.endBeat = Math.min(songBeats(), Math.max(anchor + 4, Math.ceil(b / 4) * 4));
      renderSelectionLabel();
      drawLanes();
    };
    const up = () => { window.removeEventListener("mousemove", move); window.removeEventListener("mouseup", up); };
    window.addEventListener("mousemove", move);
    window.addEventListener("mouseup", up);
  });
  canvas.addEventListener("dblclick", async (e) => {
    const rect = canvas.getBoundingClientRect();
    const beat = ((e.clientX - rect.left) / rect.width) * songBeats();
    if (clipAt(trackIndex, beat)) return showTab("roll");
    // Create a clip from this bar up to 4 bars, stopping at the next clip or the song end.
    const startBar = Math.floor(beat / 4) + 1;
    const next = (state.tracks[trackIndex].clips ?? []).map((c) => c.start_beat / 4 + 1).filter((bar) => bar > startBar);
    const endBar = Math.min(startBar + 4, state.bars + 1, ...next);
    await tool("add_clip", { track: trackIndex, start_bar: startBar, end_bar: endBar });
  });
}

const describeRange = (s) => `${trackName(s.track)} bars ${s.startBeat / 4 + 1}–${s.endBeat / 4}`;

function renderSelectionLabel() {
  $("selection-label").textContent = selections.length === 1 ? `Selected: ${describeRange(selections[0])}`
    : selections.length ? `${selections.length} ranges selected` : "";
  const has = selections.length > 0 || rollSelected.size > 0;
  $("selection-bar").classList.toggle("empty", !has);
  const barOnly = ["humanize", "duplicate", "clear", "lock", "unlock"];
  $("selection-bar").querySelectorAll("button").forEach((b) => (b.disabled = !has || (barOnly.includes(b.dataset.quick) && !selections.length)));
  $("selection-bar-label").textContent = has ? selections.map(describeRange).join(" · ") || `${rollSelected.size} notes` : "Drag across a track to select bars (Ctrl-drag adds more)";
}

function setupCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth, h = canvas.clientHeight;
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
  }
  const g = canvas.getContext("2d");
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, w, h);
  return { g, w, h };
}

function drawRuler() {
  const { g, w, h } = setupCanvas($("ruler"));
  const beats = songBeats();
  g.font = "11px Segoe UI";
  g.fillStyle = "#8a92a3";
  const barW = w / state.bars;
  for (const [i, sec] of (state.sections ?? []).entries()) {
    const x0 = ((sec.start_bar - 1) * 4 / beats) * w, x1 = Math.min(w, ((sec.end_bar - 1) * 4 / beats) * w);
    g.fillStyle = i % 2 ? "rgba(255,79,163,0.18)" : "rgba(79,209,255,0.18)";
    g.fillRect(x0, 0, x1 - x0 - 1, 9);
    g.fillStyle = "#e4e7ee";
    g.font = "9px Segoe UI";
    g.fillText(sec.name, x0 + 3, 8, x1 - x0 - 6);
  }
  g.font = "11px Segoe UI";
  g.fillStyle = "#8a92a3";
  const every = barW < 24 ? Math.ceil(24 / barW) : 1;
  for (let bar = 0; bar < state.bars; bar += every) {
    const x = (bar * 4 / beats) * w;
    g.fillRect(x, h - 6, 1, 6);
    g.fillText(String(bar + 1), x + 3, h - 8);
  }
  drawPlayhead(g, w, h);
}

function drawLanes() {
  drawRuler();
  const canvases = $("tracks").querySelectorAll("canvas");
  state.tracks.forEach((t, i) => canvases[i] && drawLane(canvases[i], t));
  const m = canvases[state.tracks.length];
  if (m) drawLane(m, { index: -2, notes: [], automation: state.masterAutomation, colour: "#e4e7ee" });
}

function drawLane(canvas, track) {
  const { g, w, h } = setupCanvas(canvas);
  const beats = songBeats();
  const x = (b) => (b / beats) * w;

  for (const s of selections.filter((s) => s.track === track.index)) {
    g.fillStyle = "rgba(79,209,255,0.16)";
    g.fillRect(x(s.startBeat), 0, x(s.endBeat) - x(s.startBeat), h);
    g.strokeStyle = "rgba(79,209,255,0.6)";
    g.strokeRect(x(s.startBeat) + 0.5, 0.5, x(s.endBeat) - x(s.startBeat) - 1, h - 1);
  }

  // Locked bars: hatched; a fully locked track is hatched end to end.
  const lockRanges = track.lock?.locked ? [{ start_beat: 0, end_beat: beats }] : track.lock?.ranges ?? [];
  for (const l of lockRanges) {
    const x0 = x(l.start_beat), x1 = x(Math.min(l.end_beat, beats));
    g.save();
    g.beginPath();
    g.rect(x0, 0, x1 - x0, h);
    g.clip();
    g.strokeStyle = "rgba(255,206,84,0.22)";
    for (let k = x0 - h; k < x1; k += 7) { g.beginPath(); g.moveTo(k, h); g.lineTo(k + h, 0); g.stroke(); }
    g.restore();
  }

  for (let bar = 0; bar <= state.bars; bar++) {
    g.fillStyle = "#2c313c";
    g.fillRect(Math.round(x(bar * 4)), 0, 1, h);
  }

  const CLIP_HEAD = 8;
  for (const c of track.clips ?? []) {
    const dragging = clipDrag && clipDrag.track === track.index && clipDrag.clip === c.clip && !clipDrag.copy;
    const x0 = x(c.start_beat), x1 = x(Math.min(c.end_beat, beats));
    g.fillStyle = track.colour;
    g.globalAlpha = dragging ? 0.05 : 0.1;
    g.fillRect(x0 + 1, 1, x1 - x0 - 2, h - 2);
    g.globalAlpha = dragging ? 0.25 : 0.55;
    g.fillRect(x0 + 1, 1, x1 - x0 - 2, CLIP_HEAD);
    g.globalAlpha = 1;
  }
  if (clipDrag && clipDrag.track === track.index && clipDrag.ghost) {
    g.strokeStyle = "#ffffff";
    g.setLineDash([3, 3]);
    g.strokeRect(x(clipDrag.ghost.start) + 1.5, 1.5, x(clipDrag.ghost.end) - x(clipDrag.ghost.start) - 3, h - 3);
    g.setLineDash([]);
  }

  for (const c of track.audioClips ?? []) {
    const x0 = x(c.start_beat), x1 = x(Math.min(c.end_beat, beats));
    g.fillStyle = track.colour;
    g.globalAlpha = 0.22;
    g.fillRect(x0, 1, x1 - x0, h - 2);
    g.globalAlpha = 0.9;
    g.fillRect(x0, 1, x1 - x0, 2);
    g.globalAlpha = 1;
    g.fillStyle = "#e4e7ee";
    g.font = "10px Segoe UI";
    g.fillText((c.looping ? "⟳ " : "") + c.name, x0 + 3, h - 6, x1 - x0 - 6);
  }

  if (track.notes?.length) {
    const pitches = track.notes.map((n) => n.pitch);
    const lo = Math.min(...pitches) - 1, hi = Math.max(...pitches) + 1;
    const rowH = (h - 4) / (hi - lo + 1);
    g.fillStyle = track.colour;
    for (const n of track.notes) {
      g.globalAlpha = 0.45 + (n.velocity / 127) * 0.55;
      g.fillRect(x(n.start), 2 + (hi - n.pitch) * rowH, Math.max(1.5, x(n.start + n.length) - x(n.start) - 1), Math.max(1.5, rowH - 0.5));
    }
    g.globalAlpha = 1;
  }

  for (const lane of track.automation ?? []) {
    g.strokeStyle = /volume/i.test(lane.param) ? "rgba(255,255,255,0.75)" : "rgba(255,206,84,0.75)";
    g.lineWidth = 1.25;
    g.beginPath();
    const pts = lane.points;
    g.moveTo(0, h - 2 - pts[0].norm * (h - 4));
    for (const p of pts) g.lineTo(x(p.beat), h - 2 - p.norm * (h - 4));
    g.lineTo(w, h - 2 - pts.at(-1).norm * (h - 4));
    g.stroke();
  }
  drawPlayhead(g, w, h);
}

function drawPlayhead(g, w, h, xOffset = 0) {
  if (!state.playing && state.positionBeats === 0) return;
  const px = xOffset + ((state.positionBeats % songBeats()) / songBeats()) * (w - xOffset);
  g.fillStyle = "#ffffff";
  g.fillRect(px, 0, 1, h);
}

$("ruler").addEventListener("mousedown", async (e) => {
  const r = e.currentTarget.getBoundingClientRect();
  await native.setPosition(Math.floor(((e.clientX - r.left) / r.width) * songBeats()));
  await refresh();
});

// ---------------------------------------------------------------------------
// Piano roll

const KEYS_W = 46;
const ROW_H = 12;
let rollTopPitch = 84;            // pitch at the top row
let rollNotes = [];               // working copy of the selected track's notes
const rollSelected = new Set();   // indices into rollNotes
let rollSelKeys = null;           // selection as pitch@start, carried across a commit
const noteKey = (n) => `${n.pitch}@${Math.round(n.start * 1000)}`;
let rollDrag = null;
let lastLength = 0.5;
let rollTrackShown = -1;

function syncRollNotes() {
  if (rollDrag) return;
  const t = state.tracks[selectedTrack];
  rollNotes = t ? t.notes.map((n) => ({ ...n })) : [];
  if (rollTrackShown !== selectedTrack && rollNotes.length) {
    const avg = rollNotes.reduce((s, n) => s + n.pitch, 0) / rollNotes.length;
    rollTopPitch = Math.min(127, Math.round(avg) + 12);
  }
  rollTrackShown = selectedTrack;
  if (rollSelKeys) {
    rollSelected.clear();
    rollNotes.forEach((n, i) => rollSelKeys.has(noteKey(n)) && rollSelected.add(i));
    rollSelKeys = null;
  }
  for (const i of [...rollSelected]) if (i >= rollNotes.length) rollSelected.delete(i);
}

function rollGeometry() {
  const canvas = $("roll");
  const w = canvas.clientWidth, h = canvas.clientHeight;
  const gridW = w - KEYS_W;
  return {
    w, h, gridW,
    xOf: (beat) => KEYS_W + (beat / songBeats()) * gridW,
    beatOf: (x) => ((x - KEYS_W) / gridW) * songBeats(),
    yOf: (pitch) => (rollTopPitch - pitch) * ROW_H,
    pitchOf: (y) => rollTopPitch - Math.floor(y / ROW_H),
  };
}

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const isBlack = (p) => [1, 3, 6, 8, 10].includes(p % 12);
const inKey = (p) => !state.key || state.key.pitch_classes.includes(p % 12);
function snapToScale(p) {
  if (!$("scale-lock").checked || !state.key) return p;
  for (let d = 0; d < 12; d++) {
    if (inKey(p - d)) return p - d;
    if (inKey(p + d)) return p + d;
  }
  return p;
}

function drawRoll() {
  if (activeTab !== "roll") return;
  const canvas = $("roll");
  const { g, w, h } = setupCanvas(canvas);
  const geo = rollGeometry();
  const track = state.tracks[selectedTrack];

  if (!track) {
    g.fillStyle = "#8a92a3";
    g.font = "13px Segoe UI";
    g.fillText(selectedTrack === -1 ? "Master bus: use the Instrument & effects tab." : "Add a track to start.", 20, 30);
    return;
  }

  const rows = Math.ceil(h / ROW_H);
  for (let r = 0; r < rows; r++) {
    const pitch = rollTopPitch - r;
    if (pitch < 0) break;
    g.fillStyle = state.key && inKey(pitch) ? (pitch % 12 === state.key.pitch_classes[0] ? "#262c3a" : "#20252f") : isBlack(pitch) ? "#15181e" : "#1b1f27";
    g.fillRect(KEYS_W, r * ROW_H, geo.gridW, ROW_H);
    g.fillStyle = isBlack(pitch) ? "#2a2f3a" : "#cfd4de";
    g.fillRect(0, r * ROW_H, KEYS_W - 2, ROW_H - 1);
    if (pitch % 12 === 0) {
      g.fillStyle = "#111318";
      g.font = "9px Segoe UI";
      g.fillText(`C${pitch / 12 - 1}`, 4, r * ROW_H + ROW_H - 2);
      g.fillStyle = "#353b48";
      g.fillRect(KEYS_W, r * ROW_H + ROW_H - 1, geo.gridW, 1);
    }
  }

  for (let b = 0; b <= songBeats(); b += SNAP) {
    const x = Math.round(geo.xOf(b));
    g.fillStyle = b % 4 === 0 ? "#3c4352" : b % 1 === 0 ? "#2a2f3a" : "#1f232b";
    if (geo.gridW / songBeats() * SNAP < 5 && b % 1 !== 0) continue;
    g.fillRect(x, 0, 1, h);
  }

  for (const s of selections.filter((s) => s.track === selectedTrack)) {
    g.fillStyle = "rgba(79,209,255,0.08)";
    g.fillRect(geo.xOf(s.startBeat), 0, geo.xOf(s.endBeat) - geo.xOf(s.startBeat), h);
  }

  rollNotes.forEach((n, i) => {
    const x = geo.xOf(n.start), y = geo.yOf(n.pitch);
    const nw = Math.max(3, geo.xOf(n.start + n.length) - x - 1);
    g.globalAlpha = 0.5 + (n.velocity / 127) * 0.5;
    g.fillStyle = track.colour;
    g.fillRect(x, y + 1, nw, ROW_H - 2);
    g.globalAlpha = 1;
    if (rollSelected.has(i)) {
      g.strokeStyle = "#ffffff";
      g.strokeRect(x + 0.5, y + 1.5, nw - 1, ROW_H - 3);
    }
  });

  drawPlayhead(g, w, h, KEYS_W);
  if (!state.playing) renderSelectionLabel(); // note selection feeds the action bar
}

function noteAt(geo, x, y) {
  const pitch = geo.pitchOf(y);
  for (let i = rollNotes.length - 1; i >= 0; i--) {
    const n = rollNotes[i];
    if (n.pitch === pitch && x >= geo.xOf(n.start) && x <= geo.xOf(n.start + n.length)) return i;
  }
  return -1;
}

const snap = (b, e) => (e.altKey ? b : Math.round(b / SNAP) * SNAP);

$("roll").addEventListener("mousedown", (e) => {
  if (!state.tracks[selectedTrack]) return;
  const r = e.currentTarget.getBoundingClientRect();
  const x = e.clientX - r.left, y = e.clientY - r.top;
  const geo = rollGeometry();
  if (x < KEYS_W) return;

  let i = noteAt(geo, x, y);

  if (e.button === 2) {
    if (i >= 0) { rollNotes.splice(i, 1); rollSelected.clear(); commitRoll(); }
    return;
  }

  if (i < 0) {
    const start = Math.max(0, Math.floor(geo.beatOf(x) / SNAP) * SNAP);
    rollNotes.push({ pitch: snapToScale(geo.pitchOf(y)), start, length: lastLength, velocity: 100 });
    i = rollNotes.length - 1;
    rollSelected.clear();
    rollSelected.add(i);
    rollDrag = { mode: "resize", index: i, originX: x, originLength: lastLength, created: true };
  } else {
    if (!rollSelected.has(i)) {
      if (!e.shiftKey) rollSelected.clear();
      rollSelected.add(i);
    }
    const n = rollNotes[i];
    const nearEnd = geo.xOf(n.start + n.length) - x < 6;
    rollDrag = nearEnd
      ? { mode: "resize", index: i, originX: x, originLength: n.length }
      : { mode: "move", index: i, originX: x, originY: y, origins: [...rollSelected].map((k) => ({ k, start: rollNotes[k].start, pitch: rollNotes[k].pitch })) };
  }
  rollDrag.moved = false;
  drawRoll();

  const move = (ev) => {
    const mx = ev.clientX - r.left, my = ev.clientY - r.top;
    const beatDelta = ((mx - rollDrag.originX) / geo.gridW) * songBeats();
    rollDrag.moved = true;
    if (rollDrag.mode === "resize") {
      const n = rollNotes[rollDrag.index];
      n.length = Math.max(SNAP / 2, snap(rollDrag.originLength + beatDelta, ev));
      n.length = Math.min(n.length, songBeats() - n.start);
    } else {
      const pitchDelta = Math.round((rollDrag.originY - my) / ROW_H);
      for (const o of rollDrag.origins) {
        const n = rollNotes[o.k];
        n.start = Math.max(0, Math.min(songBeats() - SNAP, snap(o.start + beatDelta, ev)));
        n.pitch = snapToScale(Math.max(0, Math.min(127, o.pitch + pitchDelta)));
      }
    }
    drawRoll();
  };
  const up = () => {
    window.removeEventListener("mousemove", move);
    window.removeEventListener("mouseup", up);
    const d = rollDrag;
    rollDrag = null;
    if (d.mode === "resize") lastLength = rollNotes[d.index].length;
    if (d.moved || d.created) commitRoll();
  };
  window.addEventListener("mousemove", move);
  window.addEventListener("mouseup", up);
});

$("roll").addEventListener("contextmenu", (e) => e.preventDefault());

$("roll").addEventListener("wheel", (e) => {
  e.preventDefault();
  if (e.ctrlKey && rollSelected.size) {
    for (const i of rollSelected) rollNotes[i].velocity = Math.max(1, Math.min(127, rollNotes[i].velocity + (e.deltaY < 0 ? 8 : -8)));
    drawRoll();
    clearTimeout(rollWheelCommit);
    rollWheelCommit = setTimeout(commitRoll, 300);
    return;
  }
  rollTopPitch = Math.max(20, Math.min(127, rollTopPitch + (e.deltaY < 0 ? 3 : -3)));
  drawRoll();
}, { passive: false });
let rollWheelCommit;

async function commitRoll() {
  rollSelKeys = new Set([...rollSelected].map((i) => rollNotes[i]).filter(Boolean).map(noteKey));
  await tool("add_notes", { track: selectedTrack, replace: true, notes: rollNotes });
}

function deleteSelectedNotes() {
  if (!rollSelected.size) return;
  rollNotes = rollNotes.filter((_, i) => !rollSelected.has(i));
  rollSelected.clear();
  commitRoll();
}

// ---------------------------------------------------------------------------
// Devices panel

async function renderDevices(force = false) {
  if (activeTab !== "devices") return;
  const isMaster = selectedTrack === -1;
  const t = isMaster ? { index: -1, effects: state.masterEffects, sends: [] } : state.tracks[selectedTrack];
  const buses = state.tracks.filter((b) => b.kind === "bus" && b.index !== selectedTrack);
  const sig = t ? JSON.stringify([selectedTrack, t.instrument, t.effects, t.sends, buses.map((b) => b.name), state.tracks.map((x) => x.name)]) : "none";
  if (!force && sig === devicesSignature) return;
  devicesSignature = sig;

  const panel = $("tab-devices");
  if (!t) { panel.innerHTML = `<div class="empty">No track selected.</div>`; return; }

  // "Instrument" is the engine behind the Sampled entries below; picking it bare gives a silent track.
  instrumentsCache ??= (await native.runTool("list_instruments", {})).instruments.filter((i) => i.name !== "Instrument");
  effectsCache ??= (await native.runTool("list_effects", {})).effects;

  panel.innerHTML = "";
  if (isMaster) { renderEffectCards(panel, t); return; }

  // Instrument
  const inst = document.createElement("div");
  inst.className = "device";
  const instName = t.instrument?.name ?? "";
  const patchSynth = /surge xt/i.test(instName) ? "Surge XT" : /ob-?xf/i.test(instName) ? "OB-Xf" : null;
  const surgeTrack = !!patchSynth;
  const programs = !patchSynth && t.instrument?.external ? await native.runTool("list_programs", { track: selectedTrack }) : null;
  const programMenu = programs?.ok ? programs.programs.map((p) => `<option value="${p.index}" ${p.index === programs.current ? "selected" : ""}>${escapeHtml(p.name || `Program ${p.index + 1}`)}</option>`).join("") : "";
  const patchMenu = surgeTrack ? Object.entries((await loadSynthPatches()).filter((p) => p.synth === patchSynth).reduce((g, p) => ((g[p.category] ??= []).push(p), g), {}))
    .map(([cat, list]) => `<optgroup label="${escapeHtml(cat)}">${list.map((p) => `<option value="${escapeHtml(p.name)}" ${t.instrument?.patch === p.name ? "selected" : ""}>${escapeHtml(p.name)}</option>`).join("")}</optgroup>`).join("") : "";
  inst.innerHTML = `
    <h4><span>Instrument</span></h4>
    <div class="row"><select>
      <option ${t.instrument ? "" : "selected"}>None</option>
      ${t.instrument && !t.instrument.library_instrument && !instrumentsCache.some((i) => i.name === instName) ? `<option selected>${escapeHtml(instName)}</option>` : ""}
      ${instrumentsCache.map((i) => `<option ${i.name === instName ? "selected" : ""}>${escapeHtml(i.name)}</option>`).join("")}
      ${Object.entries((await loadInstrumentLibrary()).reduce((g, x) => ((g[x.source === "User" ? `${x.family} (yours)` : x.family] ??= []).push(x), g), {}))
        .map(([fam, list]) => `<optgroup label="${list[0]?.source === "User" ? "Your instruments" : "Sampled"}: ${escapeHtml(fam.replace(" (yours)", ""))}">${list.map((x) => `<option value="lib:${escapeHtml(x.name)}" ${t.instrument?.library_instrument === x.name ? "selected" : ""}>${escapeHtml(x.name)}</option>`).join("")}</optgroup>`).join("")}
    </select></div>
    ${surgeTrack ? `<div class="row"><select class="patch"><option value="">${t.instrument?.patch ? "" : "Choose a sound…"}</option>${patchMenu}</select></div>` : ""}
    ${programMenu ? `<div class="row"><select class="program">${programMenu}</select></div>` : ""}
    <div class="row">${t.instrument ? `<button class="preview" title="Hear this track's sound (a short chord)">▶ Preview sound</button>` : ""}<button class="user-inst" title="Open the folder for your own instruments (Decent Sampler / Pianobook .dspreset or .dslibrary, or SFZ), then rescan">Your instruments…</button><button class="user-rescan" title="Find instruments you added to the folder">Rescan</button></div>
    ${instName === "4OSC" ? `<div class="row"><select class="preset"><option value="">Preset…</option>${(await loadPresets()).presets.map((p) => `<option value="${escapeHtml(p.name)}" title="${escapeHtml(p.description)}">${escapeHtml(p.category)} · ${escapeHtml(p.name)}</option>`).join("")}</select></div>` : ""}
    <div class="row">
      ${t.instrument?.external ? `<button class="open">Open plug-in UI</button>` : ""}
      ${/sampler/i.test(instName) ? `<button class="sample">Load sample…</button>` : ""}
    </div>
    <div class="params"></div>`;
  inst.querySelector("select").onchange = async (e) => {
    if (!e.target.value.startsWith("lib:")) return tool("set_instrument", { track: selectedTrack, instrument: e.target.value });
    if (busy) return logMessage("error", "Claude is still working - wait for it, or press Stop.");
    const name = e.target.value.slice(4);
    const working = logMessage("tool", `Loading ${name}…`);
    const r = await loadLibraryInstrument(selectedTrack, name);
    working.remove();
    if (r?.ok === false) logMessage("error", r.error);
    else logMessage("tool", `🎻 ${trackName(selectedTrack)} → ${r.instrument}`);
    devicesSignature = "";
    await refresh();
  };
  inst.querySelector(".open")?.addEventListener("click", () => native.openDeviceWindow(selectedTrack, "instrument"));
  inst.querySelector(".preview")?.addEventListener("click", () => native.previewNotes(selectedTrack, [48, 55, 60, 63, 67], 2));
  inst.querySelector(".user-inst")?.addEventListener("click", async () => {
    const dir = await native.openInstrumentsFolder();
    logMessage("tool", `📁 Put Decent Sampler (.dspreset or .dslibrary, e.g. from pianobook.co.uk) or SFZ instruments in ${dir}, each in its own folder with its samples, then press Rescan.`);
  });
  inst.querySelector(".user-rescan")?.addEventListener("click", async () => {
    const working = logMessage("tool", "Scanning your instruments…");
    await rescanUserInstruments(true);
    working.remove();
    await refresh();
  });
  inst.querySelector(".program")?.addEventListener("change", async (e) => {
    if (busy) return logMessage("error", "Claude is still working - wait for it, or press Stop.");
    const r = await native.runTool("set_program", { track: selectedTrack, program: +e.target.value });
    if (r?.ok === false) logMessage("error", r.error);
    else { logMessage("tool", `🎛 ${trackName(selectedTrack)} → ${r.name}`); native.previewNotes(selectedTrack, [48, 55, 60, 63, 67], 2); }
    devicesSignature = "";
    await refresh();
  });
  inst.querySelector(".patch")?.addEventListener("change", async (e) => {
    if (!e.target.value) return;
    if (busy) return logMessage("error", "Claude is still working - wait for it, or press Stop.");
    const r = await loadSynthPatch(selectedTrack, e.target.value, patchSynth);
    if (r?.ok) setTimeout(() => native.previewNotes(selectedTrack, [48, 55, 60, 63, 67], 2), 150);
    if (r?.ok === false) logMessage("error", r.error);
    else logMessage("tool", `🎛 ${trackName(selectedTrack)} → ${r.synth} "${r.patch}"`);
    devicesSignature = "";
    await refresh();
  });
  inst.querySelector(".preset")?.addEventListener("change", async (e) => {
    if (!e.target.value) return;
    if (busy) return logMessage("error", "Claude is still working - wait for it, or press Stop.");
    const r = await applyPreset(selectedTrack, e.target.value, (n, i) => native.runTool(n, i));
    if (r?.ok === false) logMessage("error", r.error);
    else logMessage("tool", `🎛 ${trackName(selectedTrack)} → ${r.preset}`);
    devicesSignature = "";
    await refresh();
  });
  inst.querySelector(".sample")?.addEventListener("click", async () => {
    const r = await native.chooseSample(selectedTrack, 60);
    if (r?.ok === false && r.error !== "cancelled") logMessage("error", r.error);
    await refresh();
  });
  panel.appendChild(inst);
  if (t.instrument) fillParams(inst.querySelector(".params"), "instrument");

  renderEffectCards(panel, t);

  // Sends
  if (buses.length && t.kind !== "bus") {
    const card = document.createElement("div");
    card.className = "device";
    card.innerHTML = `<h4><span>Sends</span></h4>` + buses.map((b) => {
      const level = t.sends.find((x) => x.bus_track === b.index)?.level_db ?? -60;
      return `<div class="param"><span class="pname">${escapeHtml(b.name)}</span>
        <input type="range" min="-60" max="6" step="0.5" value="${level}" data-bus="${b.index}">
        <span class="pval">${level <= -60 ? "off" : level.toFixed(1) + " dB"}</span></div>`;
    }).join("");
    card.querySelectorAll("input").forEach((inp) => inp.onchange = () =>
      tool("set_send", { track: selectedTrack, bus_track: Number(inp.dataset.bus), level_db: Number(inp.value) }));
    panel.appendChild(card);
  }
}

function renderEffectCards(panel, t) {
  const track = t.index;
  t.effects.forEach((fx) => {
    const card = document.createElement("div");
    card.className = "device";
    const canSidechain = track >= 0 && /comp/i.test(fx.name);
    card.innerHTML = `
      <h4><span title="${escapeHtml(fx.name)}">${escapeHtml(fx.name)}</span>
        ${fx.external ? `<button class="open" title="Open plug-in UI">UI</button>` : ""}
        <button class="remove" title="Remove effect">✕</button></h4>
      ${canSidechain ? `<div class="row"><select class="sc" title="Sidechain trigger"><option value="-1">Sidechain: none</option>${state.tracks.filter((x) => x.index !== track).map((x) => `<option value="${x.index}" ${fx.sidechain_track === x.index ? "selected" : ""}>Sidechain: ${escapeHtml(x.name)}</option>`).join("")}</select></div>` : ""}
      <div class="params"></div>`;
    card.querySelector(".open")?.addEventListener("click", () => native.openDeviceWindow(track, fx.index));
    card.querySelector(".remove").onclick = () => tool("remove_effect", { track, device: fx.index });
    card.querySelector(".sc")?.addEventListener("change", (e) => tool("set_sidechain", { track, device: fx.index, source_track: Number(e.target.value) }));
    panel.appendChild(card);
    fillParams(card.querySelector(".params"), fx.index, track);
  });

  // Add effect
  const add = document.createElement("div");
  add.className = "device";
  add.innerHTML = `<h4><span>${track < 0 ? "Add master effect" : "Add effect"}</span></h4>
    <div class="row"><select><option value="">Choose…</option>${effectsCache.map((e) => `<option>${escapeHtml(e.name)}</option>`).join("")}</select></div>`;
  add.querySelector("select").onchange = (e) => e.target.value && tool("add_effect", { track, effect: e.target.value });
  panel.appendChild(add);
}

async function fillParams(container, device, track = selectedTrack) {
  const r = await native.runTool("get_device_params", { track, device, limit: 64 });
  if (!r?.ok) return;
  const { params, total_matching } = r.result;
  for (const p of params) {
    const row = document.createElement("div");
    row.className = "param";
    row.innerHTML = `<span class="pname" title="${escapeHtml(p.name)}">${escapeHtml(p.name)}</span>
      <input type="range" min="${p.min}" max="${p.max}" step="${(p.max - p.min) / 500}" value="${p.value}">
      <span class="pval">${escapeHtml(p.display)}</span>`;
    const input = row.querySelector("input");
    input.addEventListener("input", async () => {
      const res = await native.runTool("set_device_params", { track, device, params: { [p.id]: Number(input.value) }, coalesce: true });
      if (res?.ok) row.querySelector(".pval").textContent = res.applied.split(" = ").slice(1).join(" = ");
    });
    input.addEventListener("change", () => refresh());
    container.appendChild(row);
  }
  if (total_matching > params.length) {
    const more = document.createElement("div");
    more.className = "pval";
    more.textContent = `+${total_matching - params.length} more (open the plug-in UI, or ask Claude)`;
    container.appendChild(more);
  }
}

// ---------------------------------------------------------------------------
// Layout

function showTab(tab) {
  activeTab = tab;
  if (tab === "devices") instrumentsCache = effectsCache = null; // pick up newly scanned plug-ins
  document.querySelectorAll(".tabs button").forEach((b) => b.classList.toggle("active", b.dataset.tab === tab));
  $("tab-roll").classList.toggle("hidden", tab !== "roll");
  $("tab-devices").classList.toggle("hidden", tab !== "devices");
  $("tab-drums").classList.toggle("hidden", tab !== "drums");
  devicesSignature = "";
  renderAll();
}

function renderAll() {
  if (!state) return;
  renderTopBar();
  renderLanes();
  renderSelectionLabel();
  $("bottom-track").textContent = state.tracks[selectedTrack] ? `— ${state.tracks[selectedTrack].name}` : "";
  syncRollNotes();
  drawRoll();
  drawDrums();
  renderDevices();
}

// ---------------------------------------------------------------------------
// Drum grid (16th steps, one row per sampler sound or GM drum key)

const GM_DRUMS = [[36, "Kick"], [38, "Snare"], [39, "Clap"], [42, "Closed hat"], [46, "Open hat"], [41, "Low tom"], [45, "Mid tom"], [48, "High tom"], [49, "Crash"], [51, "Ride"], [37, "Rim"], [56, "Cowbell"]];
let drumPage = 0;
const DRUM_BARS = 2, LABEL_W = 110;

function drumRows(t) {
  if (t.sounds?.length) {
    const seen = new Map();
    for (const snd of t.sounds) if (!seen.has(snd.key)) seen.set(snd.key, snd.name);
    return [...seen.entries()].sort((a, b) => a[0] - b[0]);
  }
  return GM_DRUMS;
}

function drumGeometry(t) {
  const canvas = $("drums");
  const w = canvas.clientWidth, h = canvas.clientHeight;
  const rows = drumRows(t);
  const steps = DRUM_BARS * 16;
  const rowH = Math.min(26, h / rows.length);
  return { w, h, rows, steps, rowH, stepW: (w - LABEL_W) / steps, startBeat: drumPage * DRUM_BARS * 4 };
}

function drawDrums() {
  if (activeTab !== "drums") return;
  const t = state.tracks[selectedTrack];
  const { g, w, h } = setupCanvas($("drums"));
  const maxPage = Math.max(0, Math.ceil(state.bars / DRUM_BARS) - 1);
  drumPage = Math.min(drumPage, maxPage);
  $("drum-page").textContent = `Bars ${drumPage * DRUM_BARS + 1}–${Math.min(state.bars, (drumPage + 1) * DRUM_BARS)}`;
  if (!t) { g.fillStyle = "#8a92a3"; g.fillText("Select a track.", 20, 30); return; }

  const geo = drumGeometry(t);
  g.font = "11px Segoe UI";
  geo.rows.forEach(([key, name], r) => {
    const y = r * geo.rowH;
    g.fillStyle = "#8a92a3";
    g.fillText(`${name} (${key})`, 6, y + geo.rowH * 0.65, LABEL_W - 10);
    for (let s = 0; s < geo.steps; s++) {
      const beat = geo.startBeat + s * 0.25;
      const x = LABEL_W + s * geo.stepW;
      const hit = t.notes.find((n) => n.pitch === key && Math.abs(n.start - beat) < 0.01);
      g.fillStyle = hit ? t.colour : s % 4 === 0 ? "#262b36" : "#1d2129";
      g.globalAlpha = hit ? 0.4 + (hit.velocity / 127) * 0.6 : 1;
      g.fillRect(x + 1, y + 1, geo.stepW - 2, geo.rowH - 2);
      g.globalAlpha = 1;
    }
  });
  if (state.playing) {
    const rel = (state.positionBeats % songBeats()) - geo.startBeat;
    if (rel >= 0 && rel < DRUM_BARS * 4) { g.fillStyle = "#fff"; g.fillRect(LABEL_W + (rel / 0.25) * geo.stepW, 0, 1, geo.rows.length * geo.rowH); }
  }
}

$("drums").addEventListener("mousedown", async (e) => {
  const t = state.tracks[selectedTrack];
  if (!t) return;
  const r = e.currentTarget.getBoundingClientRect();
  const geo = drumGeometry(t);
  const x = e.clientX - r.left, y = e.clientY - r.top;
  const row = Math.floor(y / geo.rowH), step = Math.floor((x - LABEL_W) / geo.stepW);
  if (x < LABEL_W || row >= geo.rows.length || step < 0 || step >= geo.steps) return;
  const key = geo.rows[row][0], beat = geo.startBeat + step * 0.25;
  if (beat >= songBeats()) return;
  const existing = t.notes.findIndex((n) => n.pitch === key && Math.abs(n.start - beat) < 0.01);
  const notes = t.notes.map((n) => ({ ...n }));
  if (existing >= 0) notes.splice(existing, 1);
  else notes.push({ pitch: key, start: beat, length: 0.25, velocity: e.shiftKey ? 127 : 100 });
  await tool("add_notes", { track: selectedTrack, replace: true, notes });
});
$("drum-prev").onclick = () => { drumPage = Math.max(0, drumPage - 1); drawDrums(); };
$("drum-next").onclick = () => { drumPage = Math.min(Math.ceil(state.bars / DRUM_BARS) - 1, drumPage + 1); drawDrums(); };

// ---------------------------------------------------------------------------
// Chat

const logEl = $("log");
let busy = false;

function logMessage(kind, text) {
  if (kind === "error") native.logEvent?.(`error shown: ${text}`);
  const div = document.createElement("div");
  div.className = `msg ${kind}`;
  div.textContent = text;
  if (kind === "user") followChat = true; // your own message always brings the chat down
  logEl.appendChild(div);
  return div;
}

// The chat follows new posts while you're at the bottom. Scroll up to read and it stays put;
// the ↓ button jumps back down (it lights up when something new arrives).
let followChat = true;
const atBottom = () => logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight < 60;
function updateScrollButton(newPost) {
  const btn = $("scroll-bottom");
  btn.classList.toggle("hidden", atBottom());
  if (atBottom()) btn.classList.remove("new");
  else if (newPost) btn.classList.add("new");
}
function followNewPosts() {
  if (followChat) logEl.scrollTop = logEl.scrollHeight;
  updateScrollButton(true);
}
logEl.addEventListener("scroll", () => { followChat = atBottom(); updateScrollButton(false); });
new MutationObserver(followNewPosts).observe(logEl, { childList: true, subtree: true, characterData: true });
logEl.addEventListener("load", followNewPosts, true); // images change the height after they load
$("scroll-bottom").onclick = () => { followChat = true; logEl.scrollTo({ top: logEl.scrollHeight, behavior: "smooth" }); };

function describeToolCall(use, result) {
  if (result?.ok === false) return `⚠ ${use.name}: ${result.error}`;
  if (result?.locked) return `🔒 ${use.name}: ${result.locked}`;
  const i = use.input ?? {};
  switch (use.name) {
    case "add_notes": return `✎ ${i.replace ? "Rewrote" : "Added to"} ${trackName(i.track)}: ${result.added} notes`;
    case "remove_notes": return `✎ Removed ${result.removed} notes from ${trackName(i.track)}`;
    case "add_track": return `＋ Track "${i.name}" (${result.instrument ?? i.instrument ?? "Surge XT"})`;
    case "remove_track": return `－ Removed track "${result.removed}"`;
    case "set_track": return `✎ ${trackName(i.track)}: ${Object.keys(i).filter((k) => k !== "track").join(", ")}`;
    case "set_tempo": return `✎ Tempo → ${i.bpm} BPM`;
    case "set_song_length": return `✎ Length → ${i.bars} bars`;
    case "set_instrument": return `✎ ${trackName(i.track)} instrument → ${result.instrument}`;
    case "add_effect": return `＋ ${result.name} on ${trackName(i.track)}`;
    case "remove_effect": return `－ ${result.removed} from ${trackName(i.track)}`;
    case "set_device_params": return `✎ ${trackName(i.track)} ${i.device === "instrument" ? "instrument" : "effect " + i.device}: ${result.applied}`;
    case "load_sample": return `✎ Loaded ${result.sample} on ${trackName(i.track)}`;
    case "remember": return `🧠 Remembered: ${i.note}`;
    case "lock": return `🔒 Locked ${trackName(i.track)}${i.start_bar ? ` bars ${i.start_bar}–${i.end_bar - 1}` : ""}`;
    case "unlock": return `🔓 Unlocked ${trackName(i.track)}${i.start_bar ? ` bars ${i.start_bar}–${i.end_bar - 1}` : ""}`;
    case "import_midi": return `♪ Imported MIDI: ${result.notes} notes`;
    case "save_song": return `💾 Saved ${result.path}`;
    case "export_song": return `⇩ Exported ${result.path}`;
    case "export_stems": return `⇩ Exported ${result.files} stems to ${result.folder}`;
    case "export_midi": return `⇩ Exported ${result.path}`;
    case "load_instrument": return `🎻 ${trackName(i.track)} → ${result.instrument}`;
    case "load_synth_patch": return `🎛 ${trackName(i.track)} → ${result.synth} "${result.patch}"`;
    case "set_program": return `🎛 ${trackName(i.track)} → ${result.name}`;
    case "list_programs": return `· ${result.programs?.length ?? 0} built-in sounds`;
    case "find_synth_patches": return `· Found ${result.count} synth patches`;
    case "separate_stems": return `🎚 Separating stems in a new window…`;
    case "find_instruments": return `· Found ${result.count} instruments`;
    case "find_midi_clips": return `🔎 MIDI clips: ${result.count} found`;
    case "apply_preset": return `🎛 ${trackName(i.track)} → preset "${result.preset}"`;
    case "list_presets": return `· Browsed ${result.presets.length} presets`;
    case "add_clip": return `▭ New clip on ${trackName(i.track)}, bars ${i.start_bar}–${i.end_bar - 1}`;
    case "move_clip": return `▭ ${i.copy ? "Copied" : "Moved"} ${trackName(i.track)} clip to bar ${i.dest_bar}`;
    case "resize_clip": return `▭ Resized ${trackName(i.track)} clip`;
    case "delete_clip": return `▭ Deleted ${trackName(i.track)} clip`;
    case "add_bus": return `＋ Bus "${i.name}"${i.effect ? " with " + i.effect : ""}`;
    case "set_send": return `→ ${trackName(i.track)} send to ${trackName(i.bus_track)}: ${i.level_db <= -60 ? "off" : i.level_db + " dB"}`;
    case "sidechain_duck": return `⇣ ${trackName(i.track)} ducks under ${trackName(i.source_track)}`;
    case "set_sidechain": return `⇣ Sidechain on ${trackName(i.track)}: ${result.sidechain}`;
    case "add_audio_clip": return `＋ Audio "${result.name}" on ${trackName(i.track)}`;
    case "remove_audio_clip": return `－ Audio "${result.removed}"`;
    case "set_sections": return `▤ Sections: ${i.sections.map((x) => x.name).join(" · ")}`;
    case "copy_bars": return `⧉ Copied bars ${i.from_bar}–${i.to_bar - 1} → bar ${i.dest_bar}${i.transpose ? ` (${i.transpose > 0 ? "+" : ""}${i.transpose})` : ""}`;
    case "clear_bars": return `✎ Cleared bars ${i.from_bar}–${i.to_bar - 1}`;
    case "groove": return `≈ Groove on ${trackName(i.track)}: ${result.notes_changed} notes`;
    case "transpose": return `✎ Transposed ${i.semitones > 0 ? "+" : ""}${i.semitones}`;
    case "set_key": return `♪ Key → ${result.key}`;
    case "save_take": return `✎ Take "${i.name}" on ${trackName(i.track)}${result.active === i.name ? " (active)" : ""}`;
    case "use_take": return `✎ ${trackName(i.track)} → take "${i.name}"`;
    case "set_automation": return `〰 Automation on ${i.track < 0 ? "master" : trackName(i.track)} ${i.param} (${i.points.length} points)`;
    case "clear_automation": return `〰 Cleared ${i.param} automation on ${i.track < 0 ? "master" : trackName(i.track)}`;
    case "fade": return `〰 Fade ${i.type} on ${i.track == null || i.track < 0 ? "master" : trackName(i.track)}, beats ${i.start_beat}–${i.end_beat}`;
    case "analyze_audio": return `🎧 Analyzed ${i.path.split(/[\\/]/).pop()}`;
    case "render_and_analyze": return "🎧 Rendered and listened to the mix";
    case "search_samples": return `🔎 Samples: ${result.count} found`;
    default: return `· ${use.name}`;
  }
}

function analysisToolOutput(r) {
  if (!r?.ok) return { result: r ?? { ok: false, error: "analysis failed" } };
  const { analysis, file, spectrogramPng } = r.value;
  const content = [{ type: "text", text: JSON.stringify({ ok: true, file, analysis }) }];
  if (spectrogramPng) content.push({ type: "image", source: { type: "base64", media_type: "image/png", data: spectrogramPng } });
  return { result: { ok: true, summary: analysis.summary }, content };
}

async function searchSamples(input) {
  const index = await native.getSampleIndex();
  const samples = Array.isArray(index) ? index : index?.samples;
  if (!samples?.length) return { ok: false, error: "No samples are available. Don't search again; build drums with load_instrument 'Standard Drum Kit (GeneralUser GS)' instead, and tell the user they can add samples with 'Sample folder…'." };
  const q = (input.query ?? "").toLowerCase().split(/\s+/).filter(Boolean);
  const hits = samples.filter((smp) =>
    (!input.category || smp.category === input.category) &&
    (!input.kind || smp.kind === input.kind) &&
    (!input.key || (smp.key ?? "").toLowerCase().startsWith(input.key.toLowerCase())) &&
    q.every((w) => smp.path.toLowerCase().includes(w)));
  return { ok: true, count: hits.length, samples: hits.slice(0, input.max_results ?? 25) };
}

let memory = "";
loadMemory(native).then((m) => (memory = m));

function systemExtra() {
  const autoMix = $("auto-mix").checked;
  return `# Producer memory\n${memory}\n\n# Mode\nAuto-mix is ${autoMix ? "ON: after writing or changing parts, apply the mix rules from producer memory (buses and sends, sidechain ducking, high-passes, levels, master chain) without being asked, then briefly say what you did in plain words." : "OFF: only make the mixing changes the user asks for."}`;
}

// Sampled instrument library (library/index.json): each instrument is a set of key regions
// loaded into a fresh Sampler.
let instrumentLibrary = null;
let userInstruments = [];   // from Documents\Synthient\Instruments (Decent Sampler / SFZ)
async function loadInstrumentLibrary() {
  instrumentLibrary ??= await fetchLibraryIndex("index.json", (index, abs) =>
    (index.instruments ?? []).map((i) => ({ ...i, ...(i.sfz ? { sfz: abs(i.sfz) } : {}), regions: (i.regions ?? []).map((r) => ({ ...r, path: abs(r.path) })) })));
  return [...instrumentLibrary, ...userInstruments];
}

async function rescanUserInstruments(announce) {
  const r = await native.rescanUserInstruments().catch(() => null);
  if (!r || r.error) { if (announce) logMessage("error", `Couldn't scan your instruments: ${r?.error ?? "unknown error"}`); return; }
  userInstruments = (r.list ?? []).map((i) => ({ ...i, source: "User", regions: [] }));
  devicesSignature = "";
  if (announce) logMessage("tool", `🎹 Your instruments: ${userInstruments.length} found${r.converted ? `, ${r.converted} Decent Sampler presets converted` : ""}${r.failed ? `, ${r.failed} couldn't be converted` : ""}.`);
}

async function loadLibraryInstrument(track, name) {
  const lib = await loadInstrumentLibrary();
  const inst = lib.find((i) => i.name.toLowerCase() === String(name).toLowerCase());
  if (!inst) return { ok: false, error: `No instrument named "${name}". Call find_instruments.` };
  // Preferred: the full multi-sample instrument (velocity layers, round-robins, loops, release samples).
  if (inst.sfz) {
    const s = await native.runTool("load_sfz", { track, path: inst.sfz, name: inst.name });
    if (s?.ok) return { ok: true, instrument: inst.name, family: inst.family, range: s.range ?? inst.range, regions: s.regions,
      ...(s.keyswitches ? { keyswitches: s.keyswitches } : {}), ...(inst.notes ? { notes: inst.notes } : {}) };
    if (/locked/i.test(s?.error ?? "")) return s;
  }
  // Fallback: one sample per key range on the basic Sampler.
  let r = await native.runTool("set_instrument", { track, instrument: "Sampler" });
  if (!r?.ok) return r;
  let loaded = 0;
  for (const g of inst.regions) {
    r = await native.runTool("load_sample", { track, path: g.path, root_note: g.root, min_note: g.lo, max_note: g.hi });
    if (r?.ok) loaded++;
  }
  if (!loaded) return { ok: false, error: `Could not load any samples for ${inst.name} (is the library installed?)` };
  return { ok: true, instrument: inst.name, family: inst.family, range: inst.range, regions: loaded };
}

// The library's indexes are fetched as files (fast) and their relative paths made absolute.
let libraryPath = null;
async function fetchLibraryIndex(file, transform) {
  try {
    libraryPath ??= await native.getLibraryPath();
    const index = await fetch(`library-data/${file}`).then((r) => (r.ok ? r.json() : {}));
    return transform(index, (rel) => `${libraryPath}\\${String(rel).replaceAll("/", "\\")}`);
  } catch {
    return [];
  }
}

// Surge XT patches (library/surge_patches.json)
let synthPatches = null;
// Words in Surge patch names/comments that suggest a warm, vintage, analog-style sound.
const WARM_WORDS = /\b(warm|analog|analogue|vintage|retro|80s|eighties|juno|jupiter|prophet|oberheim|ob-?x|moog|minimoog|poly ?six|polysix|cs-?80|vangelis|tape|lo-?fi|lofi|soft|mellow|lush|dream|dreamy|smooth|silky|velvet|choir|strings|brass|sweep|nostalg|warmth|fat|classic)\b/i;

async function loadSynthPatches() {
  // Vocoder patches need a live audio input, so they'd be silent here.
  synthPatches ??= [
    ...(await fetchLibraryIndex("surge_patches.json", (index, abs) =>
      (index.patches ?? []).filter((p) => p.category !== "Vocoder").map((p) => ({
        ...p, synth: "Surge XT", path: abs(p.path),
        tags: WARM_WORDS.test(`${p.name} ${p.comment ?? ""} ${p.category_raw ?? ""}`) ? ["warm"] : [] })))),
    ...(await fetchLibraryIndex("obxf_patches.json", (index, abs) =>
      (index.patches ?? []).map((p) => ({ ...p, path: abs(p.path) })))),
  ];
  return synthPatches;
}

async function loadSynthPatch(track, name, synth) {
  const list = await loadSynthPatches();
  const matches = list.filter((x) => x.name.toLowerCase() === String(name).toLowerCase());
  const p = matches.find((x) => !synth || x.synth.toLowerCase() === String(synth).toLowerCase()) ?? matches[0];
  if (!p) return { ok: false, error: `No synth patch named "${name}". Call find_synth_patches.` };
  const r = await native.runTool("load_synth_patch", { track, path: p.path, name: p.name });
  return r?.ok ? { ok: true, synth: p.synth, patch: p.name, category: p.category, ...(p.comment ? { comment: p.comment } : {}) } : r;
}

// 4OSC presets (ui/presets/4osc.json): applied on top of a full baseline so nothing lingers.
let presetBank = null;
async function loadPresets() {
  presetBank ??= await fetch("presets/4osc.json").then((r) => r.json()).catch(() => ({ base: {}, presets: [] }));
  return presetBank;
}

async function applyPreset(track, presetName, run = (n, i) => native.runTool(n, i)) {
  const bank = await loadPresets();
  const preset = bank.presets.find((p) => p.name.toLowerCase() === String(presetName).toLowerCase());
  if (!preset) return { ok: false, error: `No preset named "${presetName}". Call list_presets.` };
  const t = (await native.getState(false)).tracks[track];
  if (!t) return { ok: false, error: "No such track" };
  if (t.instrument?.name !== "4OSC") {
    const r = await run("set_instrument", { track, instrument: "4OSC" });
    if (!r?.ok) return r;
  }
  // Clear modulation routings left by the previous sound.
  const current = await native.runTool("get_device_params", { track, device: "instrument", filter: "mod:", limit: 200 });
  const clears = Object.fromEntries((current?.result?.params ?? []).map((p) => [p.id, 0]));
  const r = await run("set_device_params", { track, device: "instrument", params: { ...clears, ...bank.base, ...preset.params } });
  return r?.ok ? { ok: true, preset: preset.name, category: preset.category } : r;
}

const songName = () => (state.file || "Synthient song").replace(/\.tracktionedit$/, "");

// MIDI clip library (Documents\Synthient\MIDI Clips), re-indexed at most once a minute.
let midiClipCache = null, midiClipTime = 0;
async function findMidiClips(i) {
  if (!midiClipCache || Date.now() - midiClipTime > 60_000) {
    const r = await native.getMidiClipIndex().catch((e) => ({ error: String(e) }));
    if (r?.error) return { ok: false, error: `Couldn't index MIDI clips: ${r.error}` };
    midiClipCache = r.clips ?? [];
    midiClipTime = Date.now();
  }
  if (!midiClipCache.length) return { ok: true, count: 0, clips: [], note: "No MIDI clips yet. The user can put .mid files in Documents\\Synthient\\MIDI Clips." };
  const words = (i.query ?? "").toLowerCase().split(/\s+/).filter(Boolean);
  const key = (i.key ?? "").toLowerCase().replace(/\s+/g, " ").trim();
  let hits = midiClipCache
    .filter((c) => !i.kind || c.kind === i.kind)
    .filter((c) => !key || (c.key ?? "").toLowerCase() === key)
    .filter((c) => words.every((w) => `${c.name} ${c.category} ${c.kind} ${c.key ?? ""}`.toLowerCase().includes(w)));
  if (i.bpm) hits = [...hits].sort((a, b) => Math.abs((a.bpm ?? 999) - i.bpm) - Math.abs((b.bpm ?? 999) - i.bpm));
  const total = hits.length;
  hits = hits.slice(0, i.max_results ?? 20);
  return { ok: true, count: hits.length, total_matches: total, clips: hits };
}

const localTools = {
  find_instruments: async (i) => {
    const lib = await loadInstrumentLibrary();
    if (!lib.length) return { result: { ok: false, error: "The instrument library isn't installed yet." } };
    let words = (i.query ?? "").toLowerCase().split(/\s+/).filter(Boolean);
    const userOnly = words.includes("user");  // "user" means source User, not e.g. "GeneralUser GS"
    words = words.filter((w) => w !== "user");
    const hits = lib
      .filter((x) => !userOnly || x.source === "User")
      .filter((x) => !i.family || x.family === i.family.toLowerCase())
      .filter((x) => words.every((w) => `${x.name} ${x.family} ${x.articulation ?? ""} ${x.source}`.toLowerCase().includes(w)))
      .slice(0, i.max_results ?? 30)
      .map(({ name, family, articulation, source, range, notes }) => ({ name, family, articulation, source, range, ...(notes ? { notes } : {}) }));
    return { result: { ok: true, count: hits.length, instruments: hits } };
  },
  load_instrument: async (i) => ({ result: await loadLibraryInstrument(i.track, i.name) }),
  find_midi_clips: async (i) => ({ result: await findMidiClips(i) }),
  find_synth_patches: async (i) => {
    const list = await loadSynthPatches();
    if (!list.length) return { result: { ok: false, error: "Surge XT patches aren't installed (instrument library missing)." } };
    const words = (i.query ?? "").toLowerCase().split(/\s+/).filter(Boolean);
    const hits = list
      .filter((x) => !i.synth || x.synth.toLowerCase() === i.synth.toLowerCase())
      .filter((x) => !i.category || x.category.toLowerCase() === i.category.toLowerCase())
      .filter((x) => !i.warm || x.tags?.includes("warm"))
      .filter((x) => words.every((w) => `${x.name} ${x.synth} ${x.category} ${x.category_raw ?? ""} ${x.author ?? ""} ${x.comment ?? ""}`.toLowerCase().includes(w)))
      .slice(0, i.max_results ?? 30)
      .map(({ name, synth, category, author, comment, tags }) => ({ name, synth, category, author, ...(tags?.length ? { tags } : {}), ...(comment ? { comment: comment.replace(/&amp;#x0[DA];/g, " ").slice(0, 100) } : {}) }));
    return { result: { ok: true, count: hits.length, patches: hits } };
  },
  load_synth_patch: async (i) => ({ result: await loadSynthPatch(i.track, i.name, i.synth) }),
  list_presets: async (i) => {
    const bank = await loadPresets();
    const q = (i.query ?? "").toLowerCase();
    const list = bank.presets
      .filter((p) => !i.category || p.category.toLowerCase() === i.category.toLowerCase())
      .filter((p) => !q || (p.name + " " + p.description).toLowerCase().includes(q))
      .map(({ name, category, description }) => ({ name, category, description }));
    return { result: { ok: true, presets: list } };
  },
  apply_preset: async (i) => ({ result: await applyPreset(i.track, i.preset) }),
  save_song: async (i) => {
    const r = await native.saveSongQuietly(i.name ?? "");
    return { result: r?.ok ? { ok: true, path: r.value } : r };
  },
  export_song: async (i) => {
    const path = await native.defaultExportPath(i.name || songName(), i.format);
    const r = await native.exportWavTo(path);
    return { result: r?.ok ? { ok: true, path: r.value } : r };
  },
  export_stems: async (i) => {
    const folder = await native.defaultExportPath((i.name || songName()) + " stems", "");
    const r = await native.exportStems(i.format, folder);
    return { result: r?.ok ? { ok: true, folder: r.value.folder, files: r.value.files.length } : r };
  },
  export_midi: async (i) => {
    const path = await native.defaultExportPath(i.name || songName(), "mid");
    const r = await native.exportMidi(i.track ?? -1, path);
    return { result: r?.ok ? { ok: true, path: r.value } : r };
  },
  remember: async (i) => {
    memory = appendNote(memory, i.note, i.section);
    await native.writeMemory(memory);
    return { result: { ok: true, saved: i.note } };
  },
  separate_stems: async (i) => {
    const r = await native.separateStems(i.path);
    return { result: r?.ok === false ? r : { ok: true, note: "Separation started in its own window and takes a few minutes. The user can keep working. Call get_separated_stems later for the stem files." } };
  },
  get_separated_stems: async () => ({ result: lastStems ? { ok: true, source: lastStems.source, stems: lastStems.stems } : { ok: false, error: "No stems separated yet in this session." } }),
  analyze_audio: async (i) => analysisToolOutput(await native.analyzeAudio(i.path, { stems: !!i.stems, midi: !!i.midi })),
  render_and_analyze: async () => {
    const e = await native.exportWavTo("");
    return e?.ok ? analysisToolOutput(await native.analyzeAudio(e.value, {})) : { result: e };
  },
  search_samples: async (i) => ({ result: await searchSamples(i) }),
};

// Cost meter: this session and running total (kept in this browser's storage).
const cost = { session: 0, calls: 0, lastRequest: 0, cacheRead: 0, inputTotal: 0 };
let costAllTime = 0;
try { costAllTime = Number(localStorage.getItem("claudeCostTotal")) || 0; } catch {}

function renderCost() {
  const hit = cost.inputTotal ? Math.round((cost.cacheRead / cost.inputTotal) * 100) : 0;
  $("cost-meter").textContent = `Session $${cost.session.toFixed(2)} · last request $${cost.lastRequest.toFixed(2)} · all time $${costAllTime.toFixed(2)}`;
  $("cost-meter").title = `${cost.calls} API calls this session; ${hit}% of input served from cache`;
}

function recordUsage(usage, model) {
  const c = costOf(usage, model);
  cost.session += c;
  cost.lastRequest += c;
  cost.calls += 1;
  cost.cacheRead += usage.cache_read_input_tokens ?? 0;
  cost.inputTotal += (usage.input_tokens ?? 0) + (usage.cache_read_input_tokens ?? 0) + (usage.cache_creation_input_tokens ?? 0);
  costAllTime += c;
  try { localStorage.setItem("claudeCostTotal", String(costAllTime)); } catch {}
  renderCost();
}

const claude = new ClaudeSession(native, {
  onUsage: recordUsage,
  localTools,
  extraTools: MEMORY_TOOLS,
  getSystemExtra: systemExtra,
  onText: (t) => logMessage("assistant", t),
  onTool: (use, result) => logMessage("tool", describeToolCall(use, result)),
  onError: (t) => logMessage("error", t),
  onChange: async () => { devicesSignature = ""; await refresh(); },
});

function selectionContext() {
  const lines = selections.map((s) =>
    `- track ${s.track} (${trackName(s.track)}): bars ${s.startBeat / 4 + 1}-${s.endBeat / 4}, i.e. beats ${s.startBeat} to ${s.endBeat}`);
  if (rollSelected.size && state.tracks[selectedTrack]) {
    const notes = [...rollSelected].map((i) => rollNotes[i]).filter(Boolean)
      .map((n) => `{pitch ${n.pitch}, start ${n.start}, length ${n.length}, velocity ${n.velocity}}`);
    lines.push(`- selected notes on track ${selectedTrack} (${trackName(selectedTrack)}): ${notes.join(", ")}`);
  }
  return lines.length ? `\n\n[User's selection - change only this unless asked otherwise:\n${lines.join("\n")}]` : "";
}

function selectionSummary() {
  const parts = selections.map(describeRange);
  if (rollSelected.size && state.tracks[selectedTrack]) parts.push(`${rollSelected.size} notes on ${trackName(selectedTrack)}`);
  return parts.length ? `\n[${parts.join(" · ")}]` : "";
}

async function onSend(e) {
  e.preventDefault();
  const text = $("prompt").value.trim();
  if (!text || busy) return;
  $("prompt").value = "";
  await sendToClaude(text);
}

async function sendToClaude(text) {
  if (busy) return;
  logMessage("user", text + selectionSummary());
  await runClaude(text + selectionContext());
}

// Runs one Claude request with a visible working state and a clear "done" signal.
let statusTimer;
async function runClaude(content) {
  if (busy) return;
  if (bridgeBusy) return logMessage("error", "Claude Code is changing the song right now - try again in a moment.");
  busy = true;
  cost.lastRequest = 0;
  const started = Date.now();
  const errorsBefore = logEl.querySelectorAll(".msg.error").length;
  const elapsed = () => { const s = Math.round((Date.now() - started) / 1000); return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`; };

  $("send").disabled = true;
  $("send").textContent = "Working…";
  $("stop-claude").disabled = false;
  $("claude-status").className = "claude-status working";
  const tick = () => ($("claude-status").textContent = `Claude is working… ${elapsed()}`);
  tick();
  statusTimer = setInterval(tick, 1000);

  try {
    await claude.send(content);
  } catch (err) {
    logMessage("error", String(err));
  } finally {
    clearInterval(statusTimer);
    busy = false;
    $("send").disabled = false;
    $("send").textContent = "Send";
    $("stop-claude").disabled = true;
    instrumentsCache = effectsCache = null;
    await refresh();

    const failed = logEl.querySelectorAll(".msg.error").length > errorsBefore;
    const summary = `${failed ? "⚠ Finished with a problem" : "✓ Done"} in ${elapsed()} · $${cost.lastRequest.toFixed(2)}`;
    logMessage(failed ? "done failed" : "done", summary);
    $("claude-status").className = "claude-status " + (failed ? "failed" : "done");
    $("claude-status").textContent = summary;
    chime(failed);
    native.notifyDone();
  }
}

function chime(failed) {
  if (!$("chime").checked) return;
  try {
    const ctx = new AudioContext();
    const notes = failed ? [440, 330] : [660, 880];
    notes.forEach((f, i) => {
      const o = ctx.createOscillator(), g = ctx.createGain();
      o.frequency.value = f;
      o.connect(g).connect(ctx.destination);
      const t0 = ctx.currentTime + i * 0.12;
      g.gain.setValueAtTime(0.0001, t0);
      g.gain.exponentialRampToValueAtTime(0.15, t0 + 0.01);
      g.gain.exponentialRampToValueAtTime(0.0001, t0 + 0.25);
      o.start(t0);
      o.stop(t0 + 0.3);
    });
    setTimeout(() => ctx.close(), 1000);
  } catch {}
}

// ---------------------------------------------------------------------------
// Wiring

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
}

$("prompt-form").addEventListener("submit", onSend);

$("model-select").innerHTML = Object.entries(MODELS).map(([id, m]) => `<option value="${id}">${m.label}</option>`).join("");
try { const saved = localStorage.getItem("claudeModel"); if (MODELS[saved]) claude.model = saved; } catch {}
$("model-select").value = claude.model;
$("model-select").onchange = (e) => { claude.model = e.target.value; try { localStorage.setItem("claudeModel", claude.model); } catch {} };
renderCost();

// Selection actions. Claude handles the creative ones; the rest run directly.
const QUICK_PROMPTS = {
  vary: "Make 3 variations of the selected bars. For each selected track: first save_take the current notes as \"Original\", then save three takes \"Var 1\", \"Var 2\", \"Var 3\" - each is the track's full notes with only the selected bars changed (activate=false), and leave \"Original\" active. Say in one line how the variations differ.",
  simplify: "Simplify the selected bars: fewer notes, keep the core idea, key and rhythmic feel.",
  busier: "Make the selected bars busier and more energetic: add rhythmic movement, fills or passing notes, staying in key and style.",
  continue: "Continue the idea in the selected bars into the following bars of the same length on the same tracks - develop it rather than copy it. Extend the song if needed.",
};

async function quickAction(kind) {
  if (!selections.length && !rollSelected.size) return logMessage("error", "Select some bars first (drag across a track; Ctrl-drag adds more).");
  if (QUICK_PROMPTS[kind]) return sendToClaude(QUICK_PROMPTS[kind]);

  for (const s of [...selections].sort((a, b) => b.startBeat - a.startBeat)) {
    const bars = { from_bar: s.startBeat / 4 + 1, to_bar: s.endBeat / 4 + 1, tracks: [s.track] };
    if (kind === "humanize") await tool("groove", { track: s.track, start_beat: s.startBeat, end_beat: s.endBeat, humanize_timing: 0.015, humanize_velocity: 0.3 });
    if (kind === "clear") await tool("clear_bars", bars);
    if (kind === "lock") await tool("lock", { track: s.track, start_bar: bars.from_bar, end_bar: bars.to_bar });
    if (kind === "unlock") await tool("unlock", { track: s.track, start_bar: bars.from_bar, end_bar: bars.to_bar });
    if (kind === "duplicate") {
      const len = s.endBeat - s.startBeat;
      if (s.endBeat + len > songBeats()) await tool("set_song_length", { bars: Math.min(256, (s.endBeat + len) / 4) });
      const r = await tool("copy_bars", { ...bars, dest_bar: s.endBeat / 4 + 1 });
      if (r?.ok) { s.startBeat += len; s.endBeat += len; }
    }
  }
  renderAll();
}

document.querySelectorAll("[data-quick]").forEach((b) => (b.onclick = () => quickAction(b.dataset.quick)));
$("help-button").onclick = () => $("help-dialog").showModal();
$("stop-claude").disabled = true;
try { $("chime").checked = localStorage.getItem("chime") !== "off"; } catch {}
$("chime").onchange = () => { try { localStorage.setItem("chime", $("chime").checked ? "on" : "off"); } catch {} };
$("stop-claude").onclick = () => { if (busy) { claude.cancel(); logMessage("tool", "Stopping after the current step…"); } };
// Example prompts in the chat's welcome message: clicking one fills the prompt box.
const examplePrompts = {
  "Start a song": [
    "Make a darkwave intro: a pulsing 16th bass on two layered tracks (one sub, one detuned saw), lush minor pads with chorus and reverb.",
    "Start a synthwave track at 100 BPM in A minor like Magnatron 2015: gated drums, a driving octave bass, warm Juno pads and a bright arpeggio. 16 bars.",
    "Build a 174 BPM drum and bass groove: a rolling two-step break, a Reese bass that changes every 4 bars, and a dark atmospheric pad. 16 bars.",
    "Make a Tonebox-style night-drive loop: slow 85 BPM, deep bass, a lonely lead and lots of reverb.",
    "Give me a 4-bar idea to start from in any style you like, then tell me what you made.",
  ],
  "Synthwave": [
    "Add a classic 80s gated snare on 2 and 4 with a big reverb tail.",
    "Write an arpeggio on a new track that follows the chords, 16th notes, up-down pattern, with a 3/16 delay.",
    "Make the chords a Juno-style pad: slow attack, chorus, and a filter that opens over 8 bars.",
    "Write a soaring lead melody for the chorus, saw lead with portamento, and double it an octave up quietly.",
    "Add a retro tom fill at the end of every 8 bars.",
  ],
  "Drum and bass": [
    "Make the bassline revolve: keep the rhythm but change the notes every 4 bars, and give the last bar of each 8 a fill.",
    "Layer the Reese bass: one track clean sub, one detuned saw with a moving filter, and sidechain both to the kick.",
    "Chop the break so bars 7-8 get ghost snares and a hat roll into the drop.",
    "Write a liquid DnB chord progression on Rhodes-style keys with a sweet minor 9th feel.",
    "Add a wobble bass for the drop: 1/8 LFO on the filter, switching to 1/16 in the last 2 bars.",
  ],
  "Real instruments": [
    "Add a string section: cellos on the root notes, violins on a slow countermelody. Use real dynamics, soft at first and swelling.",
    "Put a grand piano on the chords, played softly with some velocity variation so it sounds human.",
    "Add pizzicato strings bouncing on 8th notes through the verse.",
    "Write a French horn line for the chorus, doubled by trombone an octave below.",
    "Add a clean electric guitar playing muted 16th chords like an 80s pop record.",
    "Bring in timpani rolls and a choir 'aah' to build into the final chorus.",
  ],
  "Change a selection (select bars first)": [
    "Make this part busier, with more 16th notes, but keep the same chords.",
    "Give me 3 variations of the selected bars as takes so I can compare them.",
    "Make this section more sparse and moody, leave space for a vocal.",
    "Humanize this: vary the timing and velocity a little.",
    "Transpose the selection up a fourth and make it sound like a pre-chorus lift.",
    "Turn this melody into a call-and-response between the lead and the pad.",
  ],
  "Sound design": [
    "Make the lead sound like the main synth in Blade Runner: brassy, slow vibrato, huge reverb.",
    "Make the bass darker and grittier, with less high end.",
    "Give the pad a slow, breathing filter movement synced to 2 bars.",
    "Make a riser FX that sweeps up over the 4 bars before the drop.",
  ],
  "Mix and arrange": [
    "Mix this for me: balance the levels, carve EQ space, and make the kick and bass work together.",
    "Make it wider: chorus on the pads, delays on the lead, keep the bass in mono.",
    "Arrange this into a full song: intro, verse, chorus, breakdown, drop and outro, using the parts we have.",
    "Add a breakdown at bar 33: strip it to pads and a filtered beat, then build back up.",
    "Master it so it's loud but still punchy, then export an MP3.",
  ],
  "Learn and analyze": [
    "Explain what sidechaining and sends do, using this song as the example.",
    "I uploaded a track. Describe its sections and sound, then make something with a similar vibe but new melodies.",
    "What would make this song sound more professional? Suggest 5 changes, then do the first one.",
  ],
};

function renderExamples() {
  const box = $("examples");
  if (!box) return;
  box.innerHTML = Object.entries(examplePrompts).map(([group, list]) =>
    `<h4>${escapeHtml(group)}</h4>${list.map((p) => `<button type="button">${escapeHtml(p)}</button>`).join("")}`).join("");
  box.querySelectorAll("button").forEach((b) => (b.onclick = () => {
    $("prompt").value = b.textContent;
    $("prompt").focus();
  }));
}
renderExamples();

$("separate-stems").onclick = async () => {
  const r = await native.separateStems("");
  if (r?.ok === false && r.error !== "cancelled") logMessage("error", r.error);
};

// Events from the native side (stems window, etc.)
window.__JUCE__?.backend?.addEventListener("claudedaw", async (e) => {
  if (e?.type === "stemsReady") {
    const names = Object.keys(e.stems ?? {});
    logMessage("tool", `🎚 Stems ready for ${e.source.split(/[\\/]/).pop()}: ${names.join(", ")}. Use "Add stems as tracks" in the stems window, or ask Claude to use them.`);
    lastStems = e;
    if ($("chime").checked) chime();
  } else if (e?.type === "bridgeCall") {
    await handleBridgeCall(e);
  } else if (e?.type === "songChanged") {
    if (e.message) logMessage("tool", e.message);
    devicesSignature = "";
    await refresh();
  }
});

// ---------------------------------------------------------------------------
// Claude Code bridge: Claude Code (the user's Claude plan, no API credits) calls the same tools
// as the chat through analysis/synthient_mcp.py -> Bridge.cpp -> here. See docs/CLAUDE_CODE.md.

const BRIDGE_TOOLS = [
  {
    name: "get_instructions",
    description: "Synthient's composing guide and the user's producer memory (mix rules, genre notes, preferences). Read it before composing, and again after remember.",
    input_schema: { type: "object", properties: {}, additionalProperties: false },
  },
  {
    name: "get_selection",
    description: "What the user has selected in Synthient right now (bar ranges and/or piano-roll notes). When something is selected, change only that unless asked otherwise.",
    input_schema: { type: "object", properties: {}, additionalProperties: false },
  },
];
const bridgeTools = {
  get_instructions: async () => ({ result: { ok: true, instructions: `${SYSTEM_PROMPT}

${systemExtra()}` } }),
  get_selection: async () => ({ result: { ok: true, selection: selectionContext().trim() || "Nothing is selected." } }),
};
// Tools that only read; anything else refreshes the screen afterwards.
const READ_ONLY = /^(get_|find_|list_|search_)/;
let bridgeBusy = false;

async function handleBridgeCall(e) {
  let reply;
  try {
    if (e.name === "__tools") {
      reply = { tools: [...TOOLS, ...MEMORY_TOOLS, ...BRIDGE_TOOLS] };
    } else if (busy && !bridgeTools[e.name]) {
      reply = { result: { ok: false, error: "The in-app Claude chat is working on the song right now. Wait until it finishes, then try again." } };
    } else {
      bridgeBusy = true;
      const use = { name: e.name, input: e.input ?? {} };
      const out = await runToolCall(native, { ...localTools, ...bridgeTools }, use.name, use.input);
      reply = { result: out.result ?? null, ...(out.content ? { content: out.content } : {}) };
      if (!bridgeTools[use.name]) logMessage("tool", `Claude Code: ${describeToolCall(use, out.result ?? {})}`);
      if (!READ_ONLY.test(use.name)) { instrumentsCache = effectsCache = null; devicesSignature = ""; await refresh(); }
    }
  } catch (err) {
    reply = { result: { ok: false, error: `Synthient error: ${err?.message ?? err}` } };
  } finally {
    bridgeBusy = false;
  }
  let json;
  try { json = JSON.stringify(reply); } catch (err) { json = JSON.stringify({ result: { ok: false, error: String(err) } }); }
  await native.bridgeReply(e.id, json);
}

$("new-chat").onclick = () => {
  if (busy) return logMessage("error", "Claude is still working - press Stop first, then Reset chat.");
  claude.reset();
  logEl.querySelectorAll(".msg:not(.system)").forEach((m) => m.remove());
  logMessage("tool", "New conversation. Claude keeps the song and producer memory, but not earlier messages.");
};
$("help-close").onclick = () => $("help-dialog").close();

$("upload-track").onclick = async () => {
  if (busy) return;
  const stems = false, midi = $("deep-analysis").checked; // stems have their own window now
  busy = true;
  const working = logMessage("tool", stems ? "Analyzing and separating stems (can take a few minutes)…" : midi ? "Analyzing and transcribing to MIDI…" : "Analyzing…");
  try {
    const r = await native.analyzeAudio("", { stems, midi });
    working.remove();
    if (r?.ok === false) { if (r.error !== "cancelled") logMessage("error", r.error); return; }
    const out = analysisToolOutput(r);
    const name = r.value.file.split(/[\\/]/).pop();
    logMessage("user", `Uploaded ${name}\n${r.value.analysis.summary ?? ""}`);
    const content = [
      { type: "text", text: `I uploaded a reference track (${r.value.file}). Here is its analysis and spectrogram. Describe it musically - parts, sections, rhythm, sound design - so we can build variations in this style.\n\n${out.content[0].text}` },
      ...out.content.slice(1),
    ];
    busy = false;
    await runClaude(content);
  } catch (err) {
    logMessage("error", String(err));
  } finally {
    working.remove();
    busy = false;
    await refresh();
  }
};

$("memory-button").onclick = async () => {
  memory = await loadMemory(native);
  $("memory-text").value = memory;
  $("memory-dialog").showModal();
};
$("memory-save").onclick = async () => {
  memory = $("memory-text").value;
  await native.writeMemory(memory);
  $("memory-dialog").close();
  logMessage("tool", "Producer memory saved.");
};
$("memory-cancel").onclick = () => $("memory-dialog").close();
try { $("auto-mix").checked = localStorage.getItem("autoMix") !== "off"; } catch {}
$("auto-mix").onchange = () => { try { localStorage.setItem("autoMix", $("auto-mix").checked ? "on" : "off"); } catch {} };

$("sample-folder").onclick = async () => {
  const working = logMessage("tool", "Indexing samples…");
  const r = await native.scanSampleFolder();
  working.remove();
  if (r?.ok === false) { if (r.error !== "cancelled") logMessage("error", r.error); return; }
  const samples = Array.isArray(r.value) ? r.value : r.value?.samples ?? [];
  logMessage("tool", `Indexed ${samples.length} samples. Claude can now search them.`);
};
$("prompt").addEventListener("keydown", (e) => { if (e.key === "Enter" && !e.shiftKey) onSend(e); });

$("play").onclick = async () => { await (state.playing ? native.stop() : native.play()); await refresh(); };
$("stop").onclick = async () => { await native.stop(); await refresh(); };
$("undo").onclick = async () => { await native.undo(); devicesSignature = ""; await refresh(); };
$("redo").onclick = async () => { await native.redo(); devicesSignature = ""; await refresh(); };
$("bpm").onchange = (e) => tool("set_tempo", { bpm: Number(e.target.value) });
$("bars").onchange = (e) => tool("set_song_length", { bars: Number(e.target.value) });
$("add-track").onclick = async () => {
  const r = await tool("add_track", { name: `Track ${state.tracks.length + 1}` });
  if (r?.ok) selectTrack(r.track);
};

$("export-menu").onchange = async (e) => {
  const [kind, fmt] = e.target.value.split(":");
  e.target.value = "";
  if (!kind) return;
  const working = logMessage("tool", "Exporting…");
  let r;
  if (kind === "audio") r = await native.exportAudio(fmt);
  if (kind === "stems") r = await native.exportStems(fmt, "");
  if (kind === "midi" && fmt === "track" && !state.tracks[selectedTrack]) { working.remove(); return logMessage("error", "Select a track first."); }
  if (kind === "midi") r = await native.exportMidi(fmt === "track" ? selectedTrack : -1, "");
  working.remove();
  if (r?.ok === false) { if (r.error !== "cancelled") logMessage("error", r.error); return; }
  logMessage("tool", kind === "stems" ? `Exported ${r.value.files.length} stems to ${r.value.folder}` : `Exported ${r.value}`);
};

// Drag and drop: audio files become audio clips, MIDI files become notes.
const AUDIO_EXT = /\.(wav|mp3|ogg|flac|aif|aiff)$/i, MIDI_EXT = /\.(mid|midi)$/i;

function dropTarget(e) {
  const lane = document.elementFromPoint(e.clientX, e.clientY)?.closest(".lane:not(.master)");
  const track = lane ? Number(lane.dataset.track) : null;
  const r = $("ruler").getBoundingClientRect();
  const beat = Math.max(0, Math.min(songBeats() - 1, ((e.clientX - r.left) / r.width) * songBeats()));
  return { track, lane, beat: Math.floor(beat / 4) * 4 };
}

const arrangementEl = $("arrangement");
arrangementEl.addEventListener("dragover", (e) => {
  if (![...e.dataTransfer.types].includes("Files")) return;
  e.preventDefault();
  arrangementEl.classList.add("dragging");
  document.querySelectorAll(".lane.drop-target").forEach((l) => l.classList.remove("drop-target"));
  dropTarget(e).lane?.classList.add("drop-target");
});
arrangementEl.addEventListener("dragleave", (e) => {
  if (!arrangementEl.contains(e.relatedTarget)) {
    arrangementEl.classList.remove("dragging");
    document.querySelectorAll(".lane.drop-target").forEach((l) => l.classList.remove("drop-target"));
  }
});
arrangementEl.addEventListener("drop", async (e) => {
  e.preventDefault();
  arrangementEl.classList.remove("dragging");
  document.querySelectorAll(".lane.drop-target").forEach((l) => l.classList.remove("drop-target"));
  const { track, beat } = dropTarget(e);

  for (const file of e.dataTransfer.files) {
    const isAudio = AUDIO_EXT.test(file.name), isMidi = MIDI_EXT.test(file.name);
    if (!isAudio && !isMidi) { logMessage("error", `${file.name}: only audio and MIDI files can be dropped`); continue; }
    if (file.size > 150 * 1024 * 1024) { logMessage("error", `${file.name} is too large (150 MB max)`); continue; }

    const working = logMessage("tool", `Importing ${file.name}…`);
    const base64 = await new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(String(reader.result).split(",")[1] ?? "");
      reader.onerror = () => reject(reader.error);
      reader.readAsDataURL(file);
    });
    const stored = await native.importMedia(file.name, base64);
    working.remove();
    if (!stored?.ok) { logMessage("error", stored?.error ?? "Import failed"); continue; }
    const path = stored.value;
    const name = file.name.replace(/\.[^.]+$/, "");

    if (isMidi) {
      const r = await tool("import_midi", track != null ? { path, track, start_beat: beat } : { path, start_beat: beat });
      if (r?.ok) logMessage("tool", `♪ Imported ${file.name}: ${r.notes} notes`);
    } else {
      let target = track;
      if (target == null) {
        const t = await tool("add_track", { name, instrument: "None" });
        if (!t?.ok) continue;
        target = t.track;
      }
      const r = await tool("add_audio_clip", { track: target, path, start_beat: beat });
      if (r?.ok) logMessage("tool", `＋ ${file.name} on ${trackName(target)} at bar ${beat / 4 + 1}`);
    }
  }
});
// Dropping outside the arrangement must not navigate the web view away.
window.addEventListener("dragover", (e) => e.preventDefault());
window.addEventListener("drop", (e) => e.preventDefault());

// Pick up newly installed plug-ins in the background (already-known ones are skipped).
native.scanPluginsNow().then((added) => {
  if (added > 0) {
    instrumentsCache = effectsCache = null;
    devicesSignature = "";
    logMessage("tool", `Found ${added} new plug-in${added === 1 ? "" : "s"}.`);
  }
});

// First-run tutorial
const tutorialSteps = [
  { icon: "🌆", title: "Welcome to Synthient", body: `<p>A music workstation with Claude built in. You describe what you want in plain words, and Claude writes the parts, picks the sounds and mixes the song. You can also do everything by hand.</p><p>This quick tour shows the main pieces. You can open it again any time from the <b>?</b> button at the top right.</p>` },
  { icon: "💬", title: "Talk to Claude", body: `<p>The chat on the right is where you ask for things:</p><ul><li>"Make a synthwave intro at 100 BPM with a pulsing bass and Juno pads"</li><li>"Make the drums busier in the chorus"</li><li>"Mix this so the kick and bass don't fight"</li></ul><p>The welcome message has dozens of examples: click one to put it in the box. Claude shows each step it takes, and <b>Stop</b> halts it.</p><ul><li>The chat follows new messages. If you scroll up to read, the <b>↓</b> button jumps back to the newest one (it lights up when something new arrives).</li><li><b>Reset chat</b> clears the conversation and starts fresh. Your song is kept, and fresh chats cost less.</li></ul>` },
  { icon: "🎚", title: "Tracks", body: `<ul><li>Each row is a track with its own instrument. <b>+ Add track</b> adds one, ✕ removes it. Double-click a name to rename it.</li><li><b>M</b> mutes, <b>S</b> solos, the slider sets the volume.</li><li>Click a track to edit it below: <b>Piano roll</b> for notes, <b>Drum grid</b> for beats, <b>Instrument &amp; effects</b> for sounds.</li><li>Press <b>Space</b> to play and pause.</li><li><b>Zoom the timeline</b> with the <b>−</b> / <b>+</b> buttons above the track names, or <b>Ctrl + mouse wheel</b> over the tracks. Scroll sideways to move along the song; <b>Fit</b> shows the whole song again.</li></ul>` },
  { icon: "🎛", title: "Sounds", body: `<ul><li><b>OB-Xf</b> (a classic Oberheim-style analog synth, ~490 sounds) is the default: warm pads, brass, basses and leads for synthwave. <b>Dexed</b> is a DX7 for 80s electric pianos and bells. <b>Surge XT</b> adds ~2,900 more modern sounds.</li><li>Pick sounds in <b>Instrument &amp; effects</b>; <b>▶ Preview sound</b> lets you hear one before you write anything. Or ask Claude: "give the lead a brassy 80s sound".</li><li><b>Sampled instruments</b>: real strings, piano, guitars, brass, choir and every General MIDI sound. They respond to how hard notes are played.</li><li>Add effects like reverb, delay and chorus from the same tab.</li></ul>` },,
  { icon: "🖱", title: "Change just one part", body: `<ul><li><b>Drag across a track</b> to select bars, then ask Claude to change only that part ("make this busier").</li><li><b>Ctrl + drag</b> selects more ranges, even on other tracks.</li><li>The bar above the tracks has quick buttons: <b>Variations ×3</b>, <b>Simplify</b>, <b>Busier</b>, <b>Continue</b>, <b>Humanize</b>.</li><li><b>Lock</b> (🔒) protects a part so Claude never changes it.</li></ul>` },
  { icon: "🎧", title: "Bring your own audio", body: `<ul><li><b>Upload track…</b> lets Claude analyse a song: tempo, key, sections and energy. Tick <b>MIDI</b> to turn it into notes.</li><li><b>Separate stems…</b> splits a song into drums, bass, vocals and other in its own window, so you can keep working.</li><li>Drag audio or MIDI files straight onto a track. <b>Sample folder…</b> lets Claude use your own samples.</li></ul>` },
  { icon: "✨", title: "Mixing help", body: `<ul><li><b>Auto-mix</b> (on by default) has Claude apply good mixing habits for you: sidechain, buses, EQ space, levels.</li><li><b>Producer memory…</b> is what Claude remembers about your taste. Tell Claude "remember I like big gated snares" and it saves it.</li><li>The cost line under the chat shows what each request cost. Sonnet is half the price of Opus.</li><li>Have a Claude subscription? <b>Claude Code</b> can compose in Synthient too, without API costs: see <b>CLAUDE_CODE.md</b> in the Synthient folder.</li></ul>` },
  { icon: "💾", title: "Saving and exporting", body: `<ul><li><b>Save</b> and <b>Open</b> are at the top left. Synthient also autosaves every 2 minutes and offers recovery after a crash.</li><li><b>Export…</b> renders WAV, MP3, OGG or FLAC, stems (one file per track) or MIDI.</li><li><b>Ctrl + Z</b> undoes anything, including Claude's changes.</li><li>If something goes wrong, Synthient keeps a log and crash reports in <b>%APPDATA%\\Synthient\\logs</b> that help find the cause.</li></ul><p>That's it. Have fun making music!</p>` },
];
let tutorialIndex = 0;
const TUTORIAL_VERSION = "2"; // bump when the tour changes so everyone sees the new version once
function tutorialHidden() { try { return localStorage.getItem("tutorialHiddenVersion") === TUTORIAL_VERSION; } catch { return false; } }
function rememberTutorialChoice() { try { localStorage.setItem("tutorialHiddenVersion", $("tutorial-hide").checked ? TUTORIAL_VERSION : ""); } catch {} }
function showTutorial(force) {
  if (!force && tutorialHidden()) return;
  tutorialIndex = 0;
  $("tutorial-hide").checked = tutorialHidden();
  renderTutorial();
  if (!$("tutorial-dialog").open) $("tutorial-dialog").showModal();
}
function renderTutorial() {
  const s = tutorialSteps[tutorialIndex];
  $("tutorial-icon").textContent = s.icon;
  $("tutorial-title").textContent = s.title;
  $("tutorial-body").innerHTML = s.body;
  $("tutorial-dots").innerHTML = tutorialSteps.map((_, i) => `<span class="${i === tutorialIndex ? "on" : ""}" data-i="${i}"></span>`).join("");
  $("tutorial-dots").querySelectorAll("span").forEach((d) => (d.onclick = () => { tutorialIndex = +d.dataset.i; renderTutorial(); }));
  $("tutorial-back").disabled = tutorialIndex === 0;
  $("tutorial-next").textContent = tutorialIndex === tutorialSteps.length - 1 ? "Start making music" : "Next";
}
function closeTutorial() { rememberTutorialChoice(); $("tutorial-dialog").close(); }
$("tutorial-back").onclick = () => { if (tutorialIndex > 0) { tutorialIndex--; renderTutorial(); } };
$("tutorial-next").onclick = () => { if (tutorialIndex < tutorialSteps.length - 1) { tutorialIndex++; renderTutorial(); } else closeTutorial(); };
$("tutorial-skip").onclick = closeTutorial;
$("tutorial-dialog").addEventListener("cancel", rememberTutorialChoice); // Esc
$("help-tutorial").onclick = () => { $("help-dialog").close(); showTutorial(true); };

// After a crash or freeze: show what the log says and ask the user to report it on GitHub.
const ISSUES_URL = "https://github.com/guyverphd/Synthient/issues";
async function showCrashReportIfNeeded() {
  const info = await native.getCrashReport().catch(() => null);
  if (!info?.crashed) return;
  $("crash-title").textContent = info.kind === "crash" ? "Synthient crashed last time"
    : info.kind === "freeze" ? "Synthient froze last time" : "Synthient didn't close normally last time";
  $("crash-time").textContent = `session started ${info.time}`;
  $("crash-log").textContent = info.log ?? "";
  $("crash-status").textContent = "";
  let zipPath = null;
  const makeReport = async () => {
    const r = await native.makeCrashReport();
    if (r?.ok === false) { $("crash-status").textContent = r.error; return null; }
    zipPath = r;
    $("crash-status").textContent = `Saved ${r}`;
    return r;
  };
  $("crash-make").onclick = makeReport;
  $("crash-mail").onclick = async () => {
    const zip = zipPath ?? (await makeReport());
    const name = zip ? zip.split(/[\\/]/).pop() : "the crash report zip";
    await native.openIssue(`Synthient ${info.kind} report (${info.time})`,
      `Synthient ${info.kind === "crash" ? "crashed" : info.kind === "freeze" ? "froze" : "closed unexpectedly"} on ${info.time}.\n\nWhat I was doing: \n\n(Drag ${name} from the folder that opened into this box.)\n`);
    $("crash-status").textContent = `Your browser should open a new GitHub issue. Drag ${name} into it. If nothing opens, copy the link and open it yourself.`;
  };
  $("crash-copy").onclick = async () => {
    try { await navigator.clipboard.writeText(ISSUES_URL); $("crash-status").textContent = "Link copied."; }
    catch { $("crash-status").textContent = ISSUES_URL; }
  };
  await new Promise((resolve) => {
    $("crash-close").onclick = () => $("crash-dialog").close();
    $("crash-dialog").addEventListener("close", resolve, { once: true });
    $("crash-dialog").showModal();
  });
}

native.getRecovery().then(async (rec) => {
  if (!rec?.available) { await showCrashReportIfNeeded(); return showTutorial(false); }
  $("recovery-time").textContent = rec.time;
  const answer = async (restore) => {
    $("recovery-dialog").close();
    const r = await native.recover(restore);
    clearSelections();
    if (r?.ok === false) logMessage("error", r.error);
    else if (restore) logMessage("tool", "Recovered the autosaved song. Use Save as to keep it.");
    await refresh();
    await showCrashReportIfNeeded();
    showTutorial(false);
  };
  $("recovery-yes").onclick = () => answer(true);
  $("recovery-no").onclick = () => answer(false);
  $("recovery-dialog").showModal();
});

document.querySelectorAll("[data-action]").forEach((b) => {
  const action = b.dataset.action;
  b.onclick = async () => {
    if (action === "saveAs") return fileAction("saveSong", true);
    if (action === "scanPlugins") { await native.scanPlugins(); instrumentsCache = effectsCache = null; devicesSignature = ""; return; }
    if (action === "audioSettings") return native.audioSettings();
    if (action === "newSong" && !confirm("Start a new song? Unsaved changes will be lost.")) return;
    return fileAction(action);
  };
});

document.querySelectorAll(".tabs button").forEach((b) => (b.onclick = () => showTab(b.dataset.tab)));

window.addEventListener("keydown", async (e) => {
  // Reloading the embedded page leaves a blank window, so block browser refresh.
  if (e.key === "F5" || (e.ctrlKey && e.key.toLowerCase() === "r")) { e.preventDefault(); return; }
  if (!state || document.querySelector("dialog[open]")) return;
  const typing = e.target.closest("textarea, input, select");
  if (typing) return;
  if (e.key === "Escape") { clearSelections(); renderAll(); }
  if (e.key === "?") $("help-dialog").showModal();
  if (e.ctrlKey && e.key.toLowerCase() === "d") { e.preventDefault(); quickAction("duplicate"); }
  if (!e.ctrlKey && e.key.toLowerCase() === "v" && selections.length) quickAction("vary");
  if (!e.ctrlKey && e.key.toLowerCase() === "l") {
    const s = selections[0];
    const track = state.tracks[s ? s.track : selectedTrack];
    const lockedHere = s ? track?.lock?.ranges?.some((r) => r.start_beat < s.endBeat && r.end_beat > s.startBeat) : track?.lock?.locked;
    if (s) quickAction(lockedHere ? "unlock" : "lock");
    else if (track) tool(lockedHere ? "unlock" : "lock", { track: track.index });
  }
  if (!e.ctrlKey && e.key.toLowerCase() === "m" && state.tracks[selectedTrack]) tool("set_track", { track: selectedTrack, mute: !state.tracks[selectedTrack].mute });
  if (!e.ctrlKey && e.key.toLowerCase() === "s" && state.tracks[selectedTrack]) tool("set_track", { track: selectedTrack, solo: !state.tracks[selectedTrack].solo });
  if (e.ctrlKey && e.key.toLowerCase() === "a" && activeTab === "roll") { e.preventDefault(); rollNotes.forEach((_, i) => rollSelected.add(i)); drawRoll(); }
  if (e.key === " ") { e.preventDefault(); $("play").click(); }
  if (e.key === "Delete" || e.key === "Backspace") {
    if (rollSelected.size && activeTab === "roll") deleteSelectedNotes();
    else if (selections.length) quickAction("clear");
  }
  if (e.ctrlKey && e.key.toLowerCase() === "z") $("undo").click();
  if (e.ctrlKey && e.key.toLowerCase() === "y") $("redo").click();
  if (e.ctrlKey && e.key.toLowerCase() === "s") { e.preventDefault(); fileAction("saveSong"); }
  if (e.ctrlKey && e.key.toLowerCase() === "o") { e.preventDefault(); fileAction("openSong"); }
});
window.addEventListener("resize", () => { applyZoom(); drawRoll(); drawDrums(); });

// Timeline zoom: 1 = the whole song fits; higher values widen the timeline, which then scrolls.
let timelineZoom = 1;
try { timelineZoom = Math.min(32, Math.max(1, +localStorage.getItem("timelineZoom") || 1)); } catch {}
function applyZoom(anchorClientX) {
  const arr = $("arrangement");
  const header = parseFloat(getComputedStyle(document.documentElement).getPropertyValue("--header-w")) || 220;
  const pad = 24;
  const viewport = Math.max(200, arr.clientWidth - pad - header);
  // Keep the bar under the mouse (or the left edge) in place while zooming.
  const anchor = anchorClientX ?? (arr.getBoundingClientRect().left + pad / 2 + header);
  const oldWidth = $("ruler").getBoundingClientRect().width || viewport;
  const beatFrac = (anchor - $("ruler").getBoundingClientRect().left) / oldWidth;
  arr.classList.toggle("zoomed", timelineZoom > 1);
  arr.style.setProperty("--timeline-w", `${Math.round(viewport * timelineZoom)}px`);
  arr.style.setProperty("--viewport-w", `${arr.clientWidth - pad}px`);
  drawLanes();
  if (timelineZoom > 1) {
    const r = $("ruler").getBoundingClientRect();
    arr.scrollLeft += r.left + beatFrac * r.width - anchor;
  } else arr.scrollLeft = 0;
  $("zoom-out").disabled = timelineZoom <= 1;
  $("zoom-fit").disabled = timelineZoom <= 1;
  $("zoom-in").disabled = timelineZoom >= 32;
}
function setZoom(z, anchorClientX) {
  timelineZoom = Math.min(32, Math.max(1, Math.round(z * 100) / 100));
  try { localStorage.setItem("timelineZoom", String(timelineZoom)); } catch {}
  applyZoom(anchorClientX);
}
$("zoom-in").onclick = () => setZoom(timelineZoom * 1.5);
$("zoom-out").onclick = () => setZoom(timelineZoom / 1.5);
$("zoom-fit").onclick = () => setZoom(1);
$("arrangement").addEventListener("wheel", (e) => {
  if (!e.ctrlKey) return;
  e.preventDefault();
  setZoom(timelineZoom * (e.deltaY < 0 ? 1.25 : 0.8), e.clientX);
}, { passive: false });
// While playing, keep the playhead in view when zoomed in.
function followPlayhead() {
  const arr = $("arrangement");
  if (timelineZoom <= 1 || !state?.playing) return;
  const r = $("ruler").getBoundingClientRect(), a = arr.getBoundingClientRect();
  const header = parseFloat(getComputedStyle(document.documentElement).getPropertyValue("--header-w")) || 220;
  const x = r.left + ((state.positionBeats % songBeats()) / songBeats()) * r.width;
  const left = a.left + 12 + header, right = a.right - 12;
  if (x > right - 20 || x < left) arr.scrollLeft += x - left - 40;
}
$("key-root").onchange = $("key-scale").onchange = () => tool("set_key", { root: $("key-root").value, scale: $("key-scale").value });
$("scale-lock").onchange = () => drawRoll();

// While playing, poll only the transport (no notes) and redraw playheads.
let polling = false;
setInterval(async () => {
  if (!state?.playing || polling) return;
  polling = true;
  const s = await native.getTransport().finally(() => (polling = false));
  state.positionBeats = s.positionBeats;
  state.playing = s.playing;
  renderPosition();
  drawLanes();
  followPlayhead();
  drawRoll();
  drawDrums();
  if (!s.playing) renderTopBar();
}, 50);

refresh().then(() => applyZoom()).finally(() => native.bridgeReady()); // Claude Code can connect from now on
rescanUserInstruments(false); // picks up instruments added while Synthient was closed

// Script errors go to the diagnostics log too (%APPDATA%\Synthient\logs\Synthient.log).
window.addEventListener("error", (e) => native.logEvent?.(`script error: ${e.message} at ${e.filename}:${e.lineno}`));
window.addEventListener("unhandledrejection", (e) => native.logEvent?.(`unhandled promise rejection: ${e.reason?.stack ?? e.reason}`));
