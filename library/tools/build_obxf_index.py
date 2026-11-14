"""Index OB-Xf's factory patches (library/plugins/OB-Xf-assets/.../Patches/<Category>/*.fxp)
into library/obxf_patches.json, in the same shape as surge_patches.json plus "synth"."""
import json
import os
import re

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(LIB, "plugins", "OB-Xf-assets", "Surge Synth Team", "OB-Xf", "Patches")

CATEGORY = {"Basses": "Bass", "Leads": "Lead", "Pads": "Pad", "Plucks": "Pluck", "Keys": "Keys", "Organs": "Keys",
            "Bells": "Keys", "Mallets": "Keys", "Brass": "Brass", "Strings": "Strings", "Winds": "Winds",
            "Voices": "Pad", "Atmospheres": "Pad", "Soundscapes": "FX", "FX": "FX", "Drums": "Percussion",
            "Percussion": "Percussion", "Guitars": "Pluck"}

patches = []
for cat in sorted(os.listdir(ROOT)):
    folder = os.path.join(ROOT, cat)
    if not os.path.isdir(folder):
        continue
    for f in sorted(os.listdir(folder)):
        if not f.lower().endswith(".fxp"):
            continue
        data = open(os.path.join(folder, f), "rb").read()
        if data[:4] != b"CcnK" or data[16:20] != b"OBXf":
            continue
        patches.append({
            "name": os.path.splitext(f)[0],
            "synth": "OB-Xf",
            "category": CATEGORY.get(cat, "Other"),
            "category_raw": cat,
            "author": "OB-Xf factory",
            "path": os.path.relpath(os.path.join(folder, f), LIB).replace("\\", "/"),
            "source": "factory",
            "tags": ["analog", "warm"],
        })

json.dump({"version": 1, "patches": patches}, open(os.path.join(LIB, "obxf_patches.json"), "w", encoding="utf-8"), indent=1)
print(len(patches), "OB-Xf patches")
