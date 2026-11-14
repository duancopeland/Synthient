"""Build library/index.json from the SFZ libraries + the extracted GeneralUser GS
regions (run sf2_extract.py first) and the GeneralUser SFZ conversion
(sf2_to_sfz.py, adds "sfz" to GeneralUser entries). See NOTES.md for the format."""
import json
import os
import re
import sys

import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sfzlib  # noqa: E402

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CC0 = "CC0 1.0"
SAL = "CC-BY 3.0 (Salamander Grand Piano by Alexander Holm, CC-BY 3.0)"
GU = "GeneralUser GS License v2.0 (free use incl. commercial; see sf2/GeneralUser-GS-LICENSE.txt)"

V = "VSCO-2-CE"
# (sfz relative to library/, name, family, articulation, source, license, options)
SFZ = [
    # --- VSCO 2 CE: strings
    (f"sfz/{V}/SViolinVib.sfz", "Solo Violin (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/SViolinVib-Quiet.sfz", "Solo Violin Soft (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/SViolinSpic.sfz", "Solo Violin Spiccato (VSCO 2)", "strings", "spiccato", {}),
    (f"sfz/{V}/SViolinPizz.sfz", "Solo Violin Pizzicato (VSCO 2)", "strings", "pizzicato", {}),
    (f"sfz/{V}/SViolinTrem.sfz", "Solo Violin Tremolo (VSCO 2)", "strings", "tremolo", {}),
    (f"sfz/{V}/ViolinEnsSusVib.sfz", "Violins (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/ViolinEnsSusVib-Quiet.sfz", "Violins Soft (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/ViolinEnsSpic.sfz", "Violins Spiccato (VSCO 2)", "strings", "spiccato", {}),
    (f"sfz/{V}/ViolinEnsPizz.sfz", "Violins Pizzicato (VSCO 2)", "strings", "pizzicato", {}),
    (f"sfz/{V}/ViolinEnsTrem.sfz", "Violins Tremolo (VSCO 2)", "strings", "tremolo", {}),
    (f"sfz/{V}/ViolaEnsSusVib.sfz", "Violas (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/ViolaEnsSusVib-Quiet.sfz", "Violas Soft (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/ViolaEnsSpic.sfz", "Violas Spiccato (VSCO 2)", "strings", "spiccato", {}),
    (f"sfz/{V}/ViolaEnsPizz.sfz", "Violas Pizzicato (VSCO 2)", "strings", "pizzicato", {}),
    (f"sfz/{V}/ViolaEnsTrem.sfz", "Violas Tremolo (VSCO 2)", "strings", "tremolo", {}),
    (f"sfz/{V}/CelloEnsSusVib.sfz", "Cellos (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/CelloEnsSusVib-Quiet.sfz", "Cellos Soft (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/CelloEnsSpic.sfz", "Cellos Spiccato (VSCO 2)", "strings", "spiccato", {}),
    (f"sfz/{V}/CelloEnsPizz.sfz", "Cellos Pizzicato (VSCO 2)", "strings", "pizzicato", {}),
    (f"sfz/{V}/CelloEnsTrem.sfz", "Cellos Tremolo (VSCO 2)", "strings", "tremolo", {}),
    (f"sfz/{V}/ContrabassSusVB.sfz", "Double Basses (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/ContrabassSusVB-Quiet.sfz", "Double Basses Soft (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/ContrabassSusNV.sfz", "Double Basses Non-Vibrato (VSCO 2)", "strings", "sustain", {}),
    (f"sfz/{V}/ContrabassSpic.sfz", "Double Basses Spiccato (VSCO 2)", "strings", "spiccato", {}),
    (f"sfz/{V}/ContrabassPizz.sfz", "Double Basses Pizzicato (VSCO 2)", "strings", "pizzicato", {}),
    (f"sfz/{V}/ContrabassTrem.sfz", "Double Basses Tremolo (VSCO 2)", "strings", "tremolo", {}),
    (f"sfz/{V}/Harp.sfz", "Harp (VSCO 2)", "strings", "plucked", {}),
    # --- woodwinds
    (f"sfz/{V}/FluteSusVib.sfz", "Flute (VSCO 2)", "woodwinds", "sustain", {}),
    (f"sfz/{V}/FluteSusNV.sfz", "Flute Non-Vibrato (VSCO 2)", "woodwinds", "sustain", {}),
    (f"sfz/{V}/FluteExpVib.sfz", "Flute Expressive (VSCO 2)", "woodwinds", "expressive", {}),
    (f"sfz/{V}/FluteStac.sfz", "Flute Staccato (VSCO 2)", "woodwinds", "staccato", {}),
    (f"sfz/{V}/PiccoloSus.sfz", "Piccolo (VSCO 2)", "woodwinds", "sustain", {}),
    (f"sfz/{V}/PiccoloStac.sfz", "Piccolo Staccato (VSCO 2)", "woodwinds", "staccato", {}),
    (f"sfz/{V}/OboeSusVib.sfz", "Oboe (VSCO 2)", "woodwinds", "sustain", {}),
    (f"sfz/{V}/OboeSusNV.sfz", "Oboe Non-Vibrato (VSCO 2)", "woodwinds", "sustain", {}),
    (f"sfz/{V}/OboeStac.sfz", "Oboe Staccato (VSCO 2)", "woodwinds", "staccato", {}),
    (f"sfz/{V}/ClarinetSus.sfz", "Clarinet (VSCO 2)", "woodwinds", "sustain", {}),
    (f"sfz/{V}/ClarinetStac.sfz", "Clarinet Staccato (VSCO 2)", "woodwinds", "staccato", {}),
    (f"sfz/{V}/BassoonSus.sfz", "Bassoon (VSCO 2)", "woodwinds", "sustain", {}),
    (f"sfz/{V}/BassoonVib.sfz", "Bassoon Vibrato (VSCO 2)", "woodwinds", "sustain", {}),
    (f"sfz/{V}/BassoonStac.sfz", "Bassoon Staccato (VSCO 2)", "woodwinds", "staccato", {}),
    # --- brass
    (f"sfz/{V}/TrumpetSus.sfz", "Trumpet (VSCO 2)", "brass", "sustain", {}),
    (f"sfz/{V}/TrumpetSusVib.sfz", "Trumpet Vibrato (VSCO 2)", "brass", "sustain", {}),
    (f"sfz/{V}/TrumpetStac.sfz", "Trumpet Staccato (VSCO 2)", "brass", "staccato", {}),
    (f"sfz/{V}/TrumpetHarmonMuteSus.sfz", "Trumpet Harmon Mute (VSCO 2)", "brass", "muted", {}),
    (f"sfz/{V}/TrumpetStraightMuteSus.sfz", "Trumpet Straight Mute (VSCO 2)", "brass", "muted", {}),
    (f"sfz/{V}/FHornSus.sfz", "French Horn (VSCO 2)", "brass", "sustain", {}),
    (f"sfz/{V}/FHornStac.sfz", "French Horn Staccato (VSCO 2)", "brass", "staccato", {}),
    (f"sfz/{V}/FHornMute.sfz", "French Horn Muted (VSCO 2)", "brass", "muted", {}),
    (f"sfz/{V}/TromboneSus.sfz", "Trombone (VSCO 2)", "brass", "sustain", {}),
    (f"sfz/{V}/TromboneVib.sfz", "Trombone Vibrato (VSCO 2)", "brass", "sustain", {}),
    (f"sfz/{V}/TromboneStac.sfz", "Trombone Staccato (VSCO 2)", "brass", "staccato", {}),
    (f"sfz/{V}/TubaSus.sfz", "Tuba (VSCO 2)", "brass", "sustain", {}),
    (f"sfz/{V}/TubaStac.sfz", "Tuba Staccato (VSCO 2)", "brass", "staccato", {}),
    # --- keys
    (f"sfz/{V}/UprightPiano.sfz", "Upright Piano (VSCO 2)", "keys", "sustain", {}),
    (f"sfz/{V}/VSUpright1.sfz", "Upright Piano 2 (VSCO 2)", "keys", "sustain", {}),
    (f"sfz/{V}/OrganLoud.sfz", "Pipe Organ Loud (VSCO 2)", "keys", "sustain", {}),
    (f"sfz/{V}/OrganQuiet.sfz", "Pipe Organ Quiet (VSCO 2)", "keys", "sustain", {}),
    (f"sfz/{V}/OrganLoudPedal.sfz", "Pipe Organ Pedal Loud (VSCO 2)", "keys", "sustain", {}),
    (f"sfz/{V}/OrganQuietPedal.sfz", "Pipe Organ Pedal Quiet (VSCO 2)", "keys", "sustain", {}),
    # --- percussion
    (f"sfz/{V}/Glockenspiel.sfz", "Glockenspiel (VSCO 2)", "percussion", "struck", {}),
    (f"sfz/{V}/Marimba.sfz", "Marimba (VSCO 2)", "percussion", "struck", {}),
    (f"sfz/{V}/Xylophone.sfz", "Xylophone (VSCO 2)", "percussion", "struck", {}),
    (f"sfz/{V}/TubularBells.sfz", "Tubular Bells (VSCO 2)", "percussion", "struck", {}),
    (f"sfz/{V}/Timpani.sfz", "Timpani (VSCO 2)", "percussion", "struck", {}),
    (f"sfz/{V}/TimpaniRolls.sfz", "Timpani Rolls (VSCO 2)", "percussion", "roll", {}),
    (f"sfz/{V}/GM-StylePerc.sfz", "Orchestral Percussion Kit (VSCO 2)", "drums", "kit", {"drums": True}),
    # --- Karoryfer
    ("sfz/Meatbass/Programs/02_arco_3vel.sfz", "Double Bass Arco (Meatbass)", "bass", "sustain", {"source": "Meatbass"}),
    ("sfz/Meatbass/Programs/04_pizz.sfz", "Double Bass Pizzicato (Meatbass)", "bass", "pizzicato", {"source": "Meatbass"}),
    ("sfz/Emilyguitar/emily_clean.sfz", "Electric Guitar Clean (Emilyguitar)", "guitar", "plucked",
     {"source": "Emilyguitar", "exclude": r"/noises/"}),
    ("sfz/Emilyguitar/emily_chords.sfz", "Electric Guitar Chords (Emilyguitar)", "guitar", "chords",
     {"source": "Emilyguitar", "exclude": r"/noises/", "sw": 33,
      "notes": "Each note plays a whole barre chord on that root, so write one note per chord. Keyswitch notes (silent) pick the chord type: 33 power chord (default), 34 minor, 35 major."}),
    ("sfz/Emilyguitar/emily_chords_wide.sfz", "Electric Guitar Chords Wide (Emilyguitar)", "guitar", "chords",
     {"source": "Emilyguitar", "exclude": r"/noises/", "sw": 33,
      "notes": "Stereo-spread version of Electric Guitar Chords. One note per chord; keyswitches 33 power (default), 34 minor, 35 major."}),
    ("sfz/Emilyguitar/emily_three_voice.sfz", "Electric Guitar Three Voice (Emilyguitar)", "guitar", "layered",
     {"source": "Emilyguitar", "exclude": r"/noises/", "notes": "Three takes of each note at once: a thick double-tracked sound."}),
    ("sfz/Emilyguitar/emily_five_voice.sfz", "Electric Guitar Five Voice (Emilyguitar)", "guitar", "layered",
     {"source": "Emilyguitar", "exclude": r"/noises/", "notes": "Five layered voices per note: a huge wall-of-guitar sound."}),
    # --- Salamander
    ("sfz/Salamander/SalamanderGrandPianoV3.sfz", "Grand Piano (Salamander)", "keys", "sustain",
     {"source": "Salamander", "license": SAL}),
]

SKIPPED_SFZ_NOTE = {
    "-KS.sfz": "VSCO keyswitch multis (articulations indexed separately)",
    "emily_basic": "duplicate of emily_clean plus release noises",
    "Meatbass/Programs/0": "other Meatbass top-level programs (modwheel xfade / 5-vel duplicates)",
    "_map.sfz": "Meatbass #include fragments",
    "Retuned": "alternative tuning of Salamander",
}

GU_DRUM_SFZ = "Drums - Standard Kit.sfz"


def safe_name(name):
    """Same file-name rule as sf2_extract.safe / sf2_to_sfz."""
    return re.sub(r'[<>:"/\\|?*]+', "-", name).strip(" .")


GM_FAMILY = ["keys", "percussion", "keys", "guitar", "bass", "strings", "strings", "brass",
             "woodwinds", "woodwinds", "synth", "synth", "fx", "ethnic", "percussion", "fx"]


def gm_family_art(p):
    fam = GM_FAMILY[p // 8]
    if p in (52, 53, 54):
        fam = "choir"
    if p in (38, 39):
        fam = "bass"
    if p in (55,):
        fam = "fx"
    if p == 47:
        fam = "percussion"
    if p in (88, 89, 90, 91, 92, 93, 94, 95):
        fam = "synth"
    art = "sustain"
    if fam in ("guitar", "bass") or p in (46, 104, 105, 106, 107, 108):
        art = "plucked"
    if p < 16 or fam == "percussion" or p == 47:
        art = "struck"
    if p in (16, 17, 18, 19, 20, 21, 22, 23):
        art = "sustain"
    if p == 44:
        art = "tremolo"
    if p == 45:
        art = "pizzicato"
    if p == 59:
        art = "muted"
    if fam == "fx":
        art = "fx"
    return fam, art


def rel(p):
    return os.path.relpath(p, LIB).replace("\\", "/")


def main():
    instruments, problems = [], []
    for path, name, fam, art, opt in SFZ:
        full = os.path.join(LIB, path)
        if not os.path.isfile(full):
            problems.append(f"missing sfz {path}"); continue
        regs, _, allr = sfzlib.reduce_instrument(full, sw=opt.get("sw"), exclude=opt.get("exclude"), drums=opt.get("drums", False))
        if not regs:
            problems.append(f"no regions: {path}"); continue
        out = [{"path": rel(r["path"]), "lo": r["lo"], "hi": r["hi"], "root": r["root"]} for r in regs]
        instruments.append({
            "name": name, "family": fam, "articulation": art,
            "source": opt.get("source", V), "license": opt.get("license", CC0),
            "sfz": path,
            **({"notes": opt["notes"]} if opt.get("notes") else {}),
            "range": [min(r["lo"] for r in out), max(r["hi"] for r in out)],
            "regions": out,
        })
    gu = os.path.join(LIB, "sf2", "GeneralUser-GS-extracted", "regions.json")
    if os.path.isfile(gu):
        for e in json.load(open(gu, encoding="utf-8")):
            if not e["regions"]:
                continue
            if e.get("drums"):
                fam, art, name = "drums", "kit", "Standard Drum Kit (GeneralUser GS)"
            else:
                fam, art = gm_family_art(e["gm_program"])
                name = f"{e['gm_name']} (GeneralUser GS)"
            entry = {
                "name": name, "family": fam, "articulation": art,
                "source": "GeneralUser GS", "license": GU,
                "gm_program": e["gm_program"], "sf2_preset": e["sf2_name"],
                "range": [min(r["lo"] for r in e["regions"]), max(r["hi"] for r in e["regions"])],
                "regions": e["regions"],
            }
            # full SFZ conversion (sf2_to_sfz.py); regions above stay as the fallback
            sfz_name = GU_DRUM_SFZ if e.get("drums") else safe_name("%03d %s" % (e["gm_program"], e["gm_name"])) + ".sfz"
            sfz_rel = "sfz/GeneralUser-GS/" + sfz_name
            if os.path.isfile(os.path.join(LIB, sfz_rel)):
                entry["sfz"] = sfz_rel
            else:
                problems.append(f"GeneralUser sfz missing (run sf2_to_sfz.py): {sfz_rel}")
            instruments.append(entry)
    else:
        problems.append("GeneralUser regions.json missing (run sf2_extract.py)")

    # verify
    bad = 0
    for ins in instruments:
        assert len(ins["regions"]) <= 64, ins["name"]
        last = -1
        for r in sorted(ins["regions"], key=lambda r: r["lo"]):
            assert r["lo"] <= r["hi"] and r["lo"] > last, (ins["name"], r)
            last = r["hi"]
            p = os.path.join(LIB, r["path"])
            try:
                info = sf.info(p)
                if info.frames <= 0:
                    raise ValueError("empty")
            except Exception as ex:  # noqa: BLE001
                bad += 1
                problems.append(f"unreadable {r['path']}: {ex}")
    with open(os.path.join(LIB, "index.json"), "w", encoding="utf-8") as f:
        json.dump({"version": 1, "root": "library/ (paths are relative to this folder)",
                   "sampler_limits": "<=64 regions, one sample per key range, no velocity layers/round robins",
                   "instruments": instruments}, f, indent=1)
    fams = {}
    for i in instruments:
        fams[i["family"]] = fams.get(i["family"], 0) + 1
    print("instruments", len(instruments), "regions", sum(len(i["regions"]) for i in instruments), "unreadable", bad)
    print(json.dumps(dict(sorted(fams.items()))))
    for p in problems[:30]:
        print("PROBLEM", p)


if __name__ == "__main__":
    main()
