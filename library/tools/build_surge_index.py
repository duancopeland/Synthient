"""Build library/surge_patches.json: a searchable index of Surge XT factory + 3rd-party patches."""
import json, re, sys
from collections import Counter
from pathlib import Path

LIB = Path(__file__).resolve().parent.parent
ROOT = LIB / "plugins" / "SurgeXTData"
OUT = LIB / "surge_patches.json"

CATS = ["Bass", "Lead", "Pad", "Pluck", "Keys", "Brass", "Strings", "Chords", "Sequence",
        "Percussion", "FX", "Vocoder", "Winds", "Polysynth", "Split", "Template", "Other"]
ALIASES = {
    "bass": "Bass", "basses": "Bass", "lead": "Lead", "leads": "Lead", "pad": "Pad", "pads": "Pad",
    "pluck": "Pluck", "plucks": "Pluck", "guitar": "Pluck", "guitars": "Pluck",
    "key": "Keys", "keys": "Keys", "piano": "Keys", "pianos": "Keys", "organ": "Keys", "organs": "Keys",
    "brass": "Brass", "string": "Strings", "strings": "Strings",
    "chord": "Chords", "chords": "Chords",
    "sequence": "Sequence", "sequences": "Sequence", "arp": "Sequence", "arps": "Sequence",
    "arpeggio": "Sequence", "arpeggios": "Sequence", "seq": "Sequence", "seqs": "Sequence",
    "drum": "Percussion", "drums": "Percussion", "percussion": "Percussion", "perc": "Percussion",
    "fx": "FX", "sfx": "FX", "effects": "FX", "effect": "FX", "atmosphere": "FX", "atmospheres": "FX",
    "vocoder": "Vocoder", "vocoders": "Vocoder", "wind": "Winds", "winds": "Winds",
    "polysynth": "Polysynth", "polysynths": "Polysynth", "poly": "Polysynth",
    "ambiance": "FX", "ambient": "FX", "soundscape": "FX", "soundscapes": "FX", "texture": "FX",
    "textures": "FX", "noise": "FX", "mallet": "Pluck", "mallets": "Pluck", "bell": "Keys", "bells": "Keys",
    "split": "Split", "splits": "Split", "template": "Template", "templates": "Template",
}
EXCLUDE_WORDS = ("template", "tutorial", "mpe")

def norm_cat(raw):
    k = (raw or "").strip().lower()
    if k in ALIASES:
        return ALIASES[k]
    for word, cat in ALIASES.items():
        if len(word) > 3 and word in k:
            return cat
    return "Other"

def unesc(s):
    for a, b in (("&lt;", "<"), ("&gt;", ">"), ("&quot;", '"'), ("&apos;", "'"), ("&amp;", "&")):
        s = s.replace(a, b)
    return s

def read_meta(p):
    d = p.read_bytes()
    i = d.find(b"<?xml")
    if i < 0:
        i = d.find(b"<patch")
    if i < 0:
        return {}
    m = re.search(rb"<meta\s[^>]*?/?>", d[i:], re.S)
    if not m:
        return {}
    tag = m.group(0).decode("utf-8", errors="replace")
    return {k: unesc(v) for k, v in re.findall(r'(\w+)\s*=\s*"([^"]*)"', tag)}

def main():
    entries, excluded = [], 0
    for source, sub in (("factory", "patches_factory"), ("3rdparty", "patches_3rdparty")):
        base = ROOT / sub
        for p in sorted(base.rglob("*.fxp"), key=lambda x: str(x).lower()):
            rel = p.relative_to(base).parts
            if source == "factory":
                folder_cat, folder_author = (rel[0] if len(rel) > 1 else ""), ""
            else:
                folder_author = rel[0]
                folder_cat = rel[1] if len(rel) > 2 else ""
            meta = read_meta(p)
            name = (meta.get("name") or "").strip() or p.stem
            raw = (meta.get("category") or "").strip() or folder_cat or "Uncategorized"
            author = (meta.get("author") or "").strip() or folder_author
            if source == "factory" and not author:
                author = "Surge Synth Team"
            if (name.lower().startswith("init") or p.stem.lower().startswith("init")
                    or any(w in x.lower() for x in (raw, folder_cat) + rel[:-1] for w in EXCLUDE_WORDS)):
                excluded += 1
                continue
            cat = norm_cat(re.split(r"[\/]", raw)[-1])
            if cat == "Other" and folder_cat:
                cat = norm_cat(folder_cat)
            if cat == "Template":
                excluded += 1
                continue
            e = {"name": name, "category": cat, "category_raw": raw, "author": author,
                 "path": p.relative_to(LIB).as_posix()}
            if meta.get("comment", "").strip():
                e["comment"] = meta["comment"].strip()
            e["source"] = source
            entries.append(e)
    seen = set()
    for e in entries:
        if e["name"].lower() in seen:
            e["name"] = f'{e["name"]} ({e["author"] or e["source"]})'
            n = 2
            while e["name"].lower() in seen:
                e["name"] = f'{e["name"].rsplit(" (", 1)[0]} ({e["author"]} {n})'
                n += 1
        seen.add(e["name"].lower())
    OUT.write_text(json.dumps({"version": 1, "root": "plugins/SurgeXTData", "patches": entries},
                              indent=1, ensure_ascii=False), encoding="utf-8")
    print("excluded:", excluded, "total:", len(entries))
    for c, n in Counter(e["category"] for e in entries).most_common():
        print(f"  {c}: {n}")
    for e in entries[::max(1, len(entries) // 5)][:5]:
        print(e)

if __name__ == "__main__":
    main()
