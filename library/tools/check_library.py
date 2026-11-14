"""Check that the instrument library is installed where Synthient expects it.

Usage: python library/tools/check_library.py [LIBRARY_DIR]
(defaults to the library/ folder this script lives in; any Python 3.8+, standard library only)

Reports, per part of docs/LIBRARY_SETUP.md, what is present and what is missing.
Exit code 0 when everything required is there, 1 otherwise.
"""
import json
import os
import sys
from collections import defaultdict

LIB = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), ".."))
REPO = os.path.dirname(LIB)
COMMON_VST3 = os.path.join(os.environ.get("CommonProgramFiles", r"C:\Program Files\Common Files"), "VST3")

problems = 0


def report(ok, label, detail=""):
    global problems
    if not ok:
        problems += 1
    print(f"  [{'ok' if ok else 'MISSING'}] {label}{(' - ' + detail) if detail else ''}")


def exists(rel):
    return os.path.exists(os.path.join(LIB, rel.replace("/", os.sep)))


def load(name):
    with open(os.path.join(LIB, name), encoding="utf-8") as f:
        return json.load(f)


print(f"Library: {LIB}\n")

# 1. Sampled instruments (index.json). The app plays an instrument's .sfz when it has one; the
#    "regions" list is only the fallback for entries without one.
print("Sampled instruments (index.json)")
by_source = defaultdict(lambda: [0, 0, None])  # total, missing, first missing path
for inst in load("index.json")["instruments"]:
    paths = [inst["sfz"]] if inst.get("sfz") else [r["path"] for r in inst.get("regions", [])]
    missing = [p for p in paths if not exists(p)]
    entry = by_source[inst["source"]]
    entry[0] += 1
    if missing:
        entry[1] += 1
        entry[2] = entry[2] or missing[0]
for source, (total, bad, first) in sorted(by_source.items()):
    report(bad == 0, f"{source}: {total - bad}/{total} instruments", f"first missing file: {first}" if bad else "")

# 2. Synth patches: Surge XT and OB-Xf patch files the patch browser and Claude load.
print("\nSynth patches")
for name, label in (("surge_patches.json", "Surge XT patches"), ("obxf_patches.json", "OB-Xf patches")):
    patches = load(name)["patches"]
    missing = [p["path"] for p in patches if not exists(p["path"])]
    report(not missing, f"{label}: {len(patches) - len(missing)}/{len(patches)}",
           f"first missing file: {missing[0]}" if missing else "")

# 3. Plug-ins: library/plugins first; a normal install in Common Files\VST3 also works (except Surge, see the guide).
print("\nPlug-ins (VST3)")
for vst3, required in (("Surge XT.vst3", True), ("Surge XT Effects.vst3", False), ("Dexed.vst3", True),
                       ("OB-Xf.vst3", True), ("juicysfplugin.vst3", False), ("CHOWTapeModel.vst3", False),
                       ("Odin2.vst3", False)):
    here = os.path.exists(os.path.join(LIB, "plugins", vst3))
    system = os.path.exists(os.path.join(COMMON_VST3, vst3))
    where = "library/plugins" if here else "Common Files\\VST3" if system else ""
    if here or system or required:
        report(here or system, vst3, where)
    else:
        print(f"  [optional] {vst3} - not installed")

# 4. TAL plug-ins: not redistributable, so each user installs them from TAL's site.
print("\nTAL plug-ins (optional, install from tal-software.com)")
for vst3 in ("TAL-NoiseMaker.vst3", "TAL-Chorus-LX.vst3", "TAL-Reverb-4.vst3"):
    found = [d for d in (os.path.join(REPO, "instruments"), COMMON_VST3) if os.path.exists(os.path.join(d, vst3))]
    print(f"  [{'ok' if found else 'optional'}] {vst3}{' - ' + found[0] if found else ' - not installed'}")

print()
if problems:
    print(f"{problems} required part(s) missing. See docs/LIBRARY_SETUP.md.")
    sys.exit(1)
print("Everything required is installed.")
