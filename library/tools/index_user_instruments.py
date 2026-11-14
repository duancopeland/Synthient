"""Index the user's own instruments (Decent Sampler presets and SFZ files).

Usage:
    python index_user_instruments.py [folder] [--force]

Default folder: %USERPROFILE%\\Documents\\Synthient\\Instruments

1. Extracts every .dslibrary (zip) to a sibling folder (skipped if already there).
2. Converts every .dspreset to a .sfz next to it (dspreset_to_sfz.py).
3. Collects every .sfz below the folder (converted or hand-dropped), skips those
   without playable regions, and writes <folder>\\user_index.json with entries in
   the library/index.json "instruments" format, but "sfz" is an ABSOLUTE path and
   "source" is "User".
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sfzlib  # noqa: E402
import dspreset_to_sfz  # noqa: E402

# Family keywords, checked in order against "<folder path> <file name>" (lower case).
# Family names follow the vocabulary already used in library/index.json.
FAMILIES = [
    ("drums", ["drum", "kit", "808", "909", "snare", "kick", "hihat", "hi-hat", "cymbal", "beat", "break"]),
    ("percussion", ["perc", "marimba", "vibraphone", "xylophone", "glock", "bell", "mallet", "timpani", "kalimba"]),
    ("keys", ["piano", "rhodes", "wurli", "epiano", "e-piano", "keys", "organ", "harpsichord", "celesta",
              "clav", "grand", "toy piano", "mellotron", "felt"]),
    ("bass", ["bass"]),
    ("guitar", ["guitar", "gtr", "ukulele", "banjo", "mandolin", "harp"]),
    ("strings", ["string", "violin", "viola", "cello", "contrabass", "fiddle", "quartet", "ensemble"]),
    ("brass", ["brass", "trumpet", "trombone", "horn", "tuba", "cornet"]),
    ("woodwinds", ["flute", "clarinet", "oboe", "bassoon", "sax", "recorder", "whistle", "woodwind"]),
    ("choir", ["choir", "vocal", "voice", "vox", "sing", "chant", "aah", "ooh"]),
    ("synth", ["pad", "synth", "tape", "cassette", "drone", "ambient", "analog", "juno", "moog",
               "prophet", "dx7", "poly", "lead", "atmos", "texture", "swell"]),
    ("fx", ["fx", "sfx", "noise", "foley", "riser", "impact", "glitch", "field"]),
]
ARTICULATIONS = [
    ("staccato", ["staccato", "stac", "short"]), ("pizzicato", ["pizz"]), ("tremolo", ["trem"]),
    ("spiccato", ["spicc"]), ("muted", ["mute"]), ("plucked", ["pluck"]), ("kit", ["kit"]),
]


def default_folder():
    return os.path.join(os.environ.get("USERPROFILE", os.path.expanduser("~")),
                        "Documents", "Synthient", "Instruments")


def words(text):
    return " " + re.sub(r"[_\-./\\()\[\]]+", " ", text.lower()) + " "


def guess(text, table, default):
    t = words(text)
    for name, keys in table:
        for k in keys:
            if (" " + k) in t:  # word-start match ("bass" must not hit "contrabass" order issues)
                return name
    return default


def playable(regions):
    out = []
    for r in regions:
        s = r.get("sample", "")
        if not s or s.startswith("*"):
            continue
        if r.get("trigger", "attack").lower() in ("release", "release_key"):
            continue
        if any(k.startswith(("on_locc", "on_hicc")) for k in r):
            continue
        out.append(r)
    return out


def entry(sfz_path, folder):
    try:
        control, regions = sfzlib.parse(sfz_path)
    except Exception as e:  # malformed file: report and skip
        print(f"SKIP  {sfz_path}: parse error {e}")
        return None
    play = playable(regions)
    if not play:
        print(f"SKIP  {sfz_path}: no playable regions")
        return None
    los, his = [], []
    for r in play:
        lo, hi, _ = sfzlib.region_keys(r)
        los.append(lo); his.append(hi)
    rel = os.path.relpath(sfz_path, folder)
    base = os.path.splitext(os.path.basename(sfz_path))[0]
    parent = os.path.basename(os.path.dirname(sfz_path))
    name = base if parent.lower() in (folder_name(folder), base.lower(), "") else f"{base} ({parent})"
    family = guess(rel, FAMILIES, "other")
    art = "kit" if family == "drums" else guess(base, ARTICULATIONS, "sustain")
    return {
        "name": name,
        "family": family,
        "articulation": art,
        "source": "User",
        "license": "see library folder",
        "sfz": os.path.abspath(sfz_path),
        "range": [min(los), max(his)],
    }


def folder_name(folder):
    return os.path.basename(os.path.normpath(folder)).lower()


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    force = "--force" in argv
    folder = os.path.abspath(args[0] if args else default_folder())
    if not os.path.isdir(folder):
        print(f"Folder not found: {folder}")
        return 1
    print(f"Scanning {folder}")
    results = dspreset_to_sfz.convert_tree(folder, force=force, quiet=True)
    converted = [r for r in results if r[0]]
    failed = len(results) - len(converted)

    instruments = []
    for dp, _, files in os.walk(folder):
        for fn in sorted(files):
            if fn.lower().endswith(".sfz"):
                e = entry(os.path.join(dp, fn), folder)
                if e:
                    instruments.append(e)
    instruments.sort(key=lambda e: (e["family"], e["name"].lower()))

    out = os.path.join(folder, "user_index.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"version": 1, "root": "absolute paths", "instruments": instruments}, f, indent=1)

    print(f"\nDecent Sampler presets: {len(results)} found, {len(converted)} converted, {failed} failed")
    warn = {}
    for _, _, w in results:
        for k, n in w.items():
            warn[k] = warn.get(k, 0) + n
    for k, n in sorted(warn.items()):
        print(f"  warning x{n}: {k}")
    print(f"Instruments indexed: {len(instruments)} -> {out}")
    for e in instruments:
        print(f"  [{e['family']:<10}] {e['name']}  {e['range'][0]}-{e['range'][1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
