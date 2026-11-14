"""Index the user's MIDI clips so Claude can search them (find_midi_clips).

Usage:
    python index_midi_clips.py [folder]

Default folder: %USERPROFILE%\\Documents\\Synthient\\MIDI Clips

Reads every .mid/.midi below the folder and writes <folder>\\clip_index.json:
{"version": 1, "clips": [{"path" (relative), "name", "category", "kind" ("drums"/"tonal"),
"bpm", "key", "beats", "bars", "notes", "lowest", "highest" (MIDI numbers), "polyphony"}]}
Tempo and key come from the file name ("... A Minor 104 bpm") when present, otherwise from
the file's tempo event; key may be null. Plain Python, no packages.
Last stdout line: {"ok": true, "folder", "index", "clips", "failed"}.
"""
import json
import os
import re
import struct
import sys

NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
# "A Minor", "C# Maj", "Eb min" (words, any case) or "Am", "F#m" (lower-case m only, so "FM" is not F minor)
KEY_WORD_RE = re.compile(r"(?<![\w#])([A-G])([#b]?)\s+((?i:major|minor|maj|min))(?![\w#])")
KEY_SHORT_RE = re.compile(r"(?<![\w#])([A-G])([#b]?)(m)(?![\w#])")
BPM_RE = re.compile(r"(\d{2,3}(?:\.\d+)?)\s*bpm", re.I)
DRUM_WORDS = re.compile(r"drum|beat|kit|perc|break|groove|hat|snare|kick", re.I)


def default_folder():
    return os.path.join(os.environ.get("USERPROFILE", os.path.expanduser("~")), "Documents", "Synthient", "MIDI Clips")


def read_vlq(b, i):
    v = 0
    while True:
        c = b[i]; i += 1
        v = (v << 7) | (c & 0x7F)
        if c < 0x80:
            return v, i


def parse_mid(path):
    """(ppq, tempo_bpm or None, notes [(start_tick, end_tick, key, vel, channel)], end_tick)."""
    with open(path, "rb") as f:
        b = f.read()
    if b[:4] != b"MThd":
        raise ValueError("not a MIDI file")
    hlen = struct.unpack(">I", b[4:8])[0]
    _, ntrk, div = struct.unpack(">HHH", b[8:14])
    if div & 0x8000:
        raise ValueError("SMPTE time division not supported")
    ppq, i, tempo, notes, end = div, 8 + hlen, None, [], 0
    for _ in range(ntrk):
        if b[i:i + 4] != b"MTrk":
            break
        tlen = struct.unpack(">I", b[i + 4:i + 8])[0]
        j, stop, t, run, open_ = i + 8, i + 8 + tlen, 0, 0, {}
        while j < stop:
            d, j = read_vlq(b, j)
            t += d
            st = b[j]
            if st == 0xFF:
                typ = b[j + 1]
                ln, j = read_vlq(b, j + 2)
                if typ == 0x51 and ln == 3 and tempo is None:
                    tempo = 60_000_000 / int.from_bytes(b[j:j + 3], "big")
                j += ln
                continue
            if st in (0xF0, 0xF7):
                ln, j = read_vlq(b, j + 1)
                j += ln
                continue
            if st & 0x80:
                run, j = st, j + 1
            hi, ch = run & 0xF0, run & 0x0F
            n = 1 if hi in (0xC0, 0xD0) else 2
            data = b[j:j + n]
            j += n
            if hi == 0x90 and data[1] > 0:
                open_.setdefault((ch, data[0]), []).append((t, data[1]))
            elif hi in (0x80, 0x90):
                lst = open_.get((ch, data[0]))
                if lst:
                    s, v = lst.pop(0)
                    notes.append((s, t, data[0], v, ch))
        end = max(end, t)
        i = stop
    return ppq, tempo, notes, end


def clip_entry(path, folder):
    ppq, tempo, notes, end = parse_mid(path)
    if not notes:
        return None
    rel = os.path.relpath(path, folder).replace("\\", "/")
    base = os.path.splitext(os.path.basename(path))[0].strip()
    parts = rel.split("/")[:-1]
    # drop a leading source folder like "Ableton" from the category
    category = "/".join(parts[1:] if len(parts) > 1 and parts[0].lower() in ("ableton",) else parts)
    m = BPM_RE.search(base)
    bpm = float(m.group(1)) if m else (round(tempo, 2) if tempo else None)
    key = None
    k = KEY_WORD_RE.search(base) or KEY_SHORT_RE.search(base)
    if k:
        mode = "major" if k.group(3).lower().startswith("maj") else "minor"
        key = f"{k.group(1)}{k.group(2)} {mode}"
    drums = any(n[4] == 9 for n in notes) or bool(DRUM_WORDS.search(category.split("/")[0] if category else base))
    beats = max(end, max(n[1] for n in notes)) / ppq
    beats = max(1.0, round(beats * 4) / 4)
    # polyphony: most notes sounding at once
    ev = sorted([(n[0], 1) for n in notes] + [(n[1], -1) for n in notes])
    cur = poly = 0
    for _, d in ev:
        cur += d
        poly = max(poly, cur)
    keys = sorted({n[2] for n in notes})
    name = re.sub(r"\s+", " ", BPM_RE.sub("", base)).strip()
    return {
        "path": rel, "name": name or base, "category": category, "kind": "drums" if drums else "tonal",
        "bpm": bpm, "key": key, "beats": beats, "bars": round(beats / 4, 2), "notes": len(notes),
        "lowest": keys[0], "highest": keys[-1],  # MIDI note numbers (60 = middle C)
        "polyphony": poly,
    }


def main(argv):
    folder = os.path.abspath(argv[0] if argv else default_folder())
    clips, failed = [], 0
    if os.path.isdir(folder):
        for dp, _, files in os.walk(folder):
            for fn in sorted(files):
                if fn.lower().endswith((".mid", ".midi")):
                    try:
                        e = clip_entry(os.path.join(dp, fn), folder)
                        if e:
                            clips.append(e)
                    except Exception as ex:  # malformed file: report and skip
                        failed += 1
                        print(f"SKIP  {os.path.join(dp, fn)}: {ex}")
        clips.sort(key=lambda c: (c["category"].lower(), c["name"].lower()))
        out = os.path.join(folder, "clip_index.json")
        with open(out, "w", encoding="utf-8") as f:
            json.dump({"version": 1, "root": "paths relative to this folder", "clips": clips}, f, indent=1)
    else:
        out = None
    print(json.dumps({"ok": True, "folder": folder, "index": out, "clips": len(clips), "failed": failed}))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
