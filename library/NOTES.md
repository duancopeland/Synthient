# ClaudeDAW instrument library

Installed 2026-10-30. Everything lives under `D:\ClaudeDAW\library\`. Nothing was installed
system-wide and no installers were run. The only file written outside this folder is the OB-Xf
asset copy described below.

## Layout and sizes

"Apparent" is the byte total. "On disk" is much larger because drive D: uses large clusters,
so every small file takes a lot of space.

| Path | What | Apparent | On disk |
|---|---|---|---|
| `plugins/Surge XT.vst3`, `plugins/Surge XT Effects.vst3` | Surge XT 1.3.4 (portable zip), GPL-3.0 | 30 MB | |
| `plugins/SurgeXTData/` | Surge factory data (patches, wavetables, skins, ...) | 367 MB | |
| `plugins/Dexed.vst3` | Dexed 1.0.1, GPL-3.0 | 7 MB | |
| `plugins/OB-Xf.vst3` | OB-Xf 1.0.3, GPL-3.0 | 8 MB | |
| `plugins/OB-Xf-assets/Surge Synth Team/OB-Xf/` | OB-Xf patches + theme (reference copy) | 24 MB | |
| `plugins/juicysfplugin.vst3` | juicysfplugin 3.1.0 (single-file VST3), GPL-3.0 | 14 MB | |
| `plugins/licenses/` | LICENSE/README for each plugin | | |
| (plugins total) | | 448 MB | 5.3 GB |
| `sfz/VSCO-2-CE/` | Versilian Community Sample Library 2 CE 1.1.0, CC0 | 3.1 GB | 5.2 GB |
| `sfz/Meatbass/` | Karoryfer Meatbass 1.001, CC0 | 283 MB | 588 MB |
| `sfz/Emilyguitar/` | Karoryfer Emilyguitar 1.001, CC0 | 120 MB | 331 MB |
| `sfz/Salamander/` | Salamander Grand Piano V3+20161209 44.1k/16-bit, CC-BY 3.0 | 1.2 GB | 1.5 GB |
| `sf2/GeneralUser-GS.sf2` (+ LICENSE/README/CHANGELOG) | GeneralUser GS 2.0.3 (GitHub repo zip, no releases exist) | 32 MB | |
| `sf2/GeneralUser-GS-extracted/` | WAV renders of GM presets + standard kit | 980 MB | |
| `tools/` | Index scripts + `sfizz/` (sfizz_render.exe, sfizz.dll) | 4 MB | |
| `index.json` | Sampler index | 0.4 MB | |
| **Total** | 11,391 files | **6.0 GB** | **15 GB** |

**Credit line (required by CC-BY):** Salamander Grand Piano by Alexander Holm, CC-BY 3.0.
It is also saved in `sfz/Salamander/CREDIT.txt`.

GeneralUser GS license v2.0 allows free use, including commercial use (`sf2/GeneralUser-GS-LICENSE.txt`).
VSCO 2 CE, Meatbass and Emilyguitar are CC0. Each project's LICENSE/README sits next to it.

## Plugins: runtime data caveats

- **Surge XT**: on Windows, Surge starts at the folder holding its DLL and walks up the parent
  folders looking for `SurgeXTData` (checked in `src/common/SurgeStorage.cpp` @ release_xt_1.3.4).
  `plugins/SurgeXTData` sits next to the `.vst3` bundles, so it gets found. Keep the two together.
  User data still goes to `Documents\Surge XT` unless a `SurgeXTUserData` folder exists further up.
- **OB-Xf**: it has no portable lookup. sst-plugininfra `paths_windows.cpp` gives two factory
  locations: `%LOCALAPPDATA%\Surge Synth Team\OB-Xf` is tried first, then
  `%ProgramData%\Surge Synth Team\OB-Xf`. Patches and theme were copied to
  `%LOCALAPPDATA%\Surge Synth Team\OB-Xf` (591 files, 24 MB, inside the user profile).
  On first run the plugin creates `Documents\Surge Synth Team\OB-Xf` for user data.
  The `OBXF_DOCUMENT_FOLDER` environment variable can redirect that folder.
- **sfizz**: the 1.2.3 `win64.zip` is the library/SDK build and contains **no VST3**. The release
  has no Windows plugin package. `sfizz_render.exe` and `sfizz.dll` were kept in `tools/sfizz/`
  for offline SFZ rendering.
- **juicysfplugin**: a VST3 is present (`plugins/juicysfplugin.vst3`, a single-file VST3).
  The VST2 and standalone builds were not kept.
- **Dexed**: no extra data is needed. Its cartridges are built in.

## index.json

```json
{"version": 1, "instruments": [
  {"name": "Cellos Pizzicato (VSCO 2)", "family": "strings", "articulation": "pizzicato",
   "source": "VSCO-2-CE", "license": "CC0 1.0", "sfz": "sfz/VSCO-2-CE/CelloEnsPizz.sfz",
   "range": [36, 77],
   "regions": [{"path": "sfz/VSCO-2-CE/Strings/Cello Section/pizzT/pizzT_C1_v2_RR1.wav", "lo": 36, "hi": 37, "root": 36}]}
]}
```

- Paths are relative to `library/` and use forward slashes. Key ranges never overlap, and every
  instrument has 64 or fewer regions.
- GeneralUser entries also carry `gm_program` (0-127, or null for the drum kit) and `sf2_preset`.
- Families: strings, woodwinds, brass, keys, guitar, bass, percussion, choir, synth, ethnic, fx, drums.
- Instrument counts: 200 total, with 2,258 regions.

| Family | Count |
|---|---|
| strings | 38 |
| woodwinds | 30 |
| keys | 23 |
| percussion | 23 |
| brass | 21 |
| fx | 17 |
| synth | 16 |
| bass | 10 |
| guitar | 9 |
| ethnic | 8 |
| choir | 3 |
| drums | 2 |

The 200 instruments come from 71 SFZ instruments (VSCO 67, Meatbass 2, Emilyguitar 1, Salamander 1)
plus 129 GeneralUser presets (128 GM programs and the standard kit).

### Verification

Every region path exists and opens with libsndfile. All files are 44.1 kHz WAV. Sample durations:

- VSCO and Salamander files are stereo.
- Meatbass and Emilyguitar files are mono.
- VSCO sustains run 4-18 s, staccatos 0.4-2 s.
- Salamander runs 3.8-25.8 s.
- GeneralUser looped sounds are 3 s, decaying ones up to 8 s.

Spot pitch check (pyin) of the GeneralUser renders: the detected pitch matches `root` within
0.2 semitone. Exception: Church Organ, which contains a 16' octave-down rank.

## How the index was built (`tools/`)

Run every script with `D:\ClaudeDAW\analysis\.venv\Scripts\python.exe`. No packages were added
to the venv: numpy, soundfile and librosa were already there.

1. `sf2_extract.py`: a custom SF2 parser. It handles preset and instrument global zones,
   preset/instrument range intersection, additive tuning and envelope generators, overriding
   root key, and scaleTuning. For each GM program (bank 0) and bank 128 preset 0, it does this:
   - It takes the zones that sound at velocity 100 on each key. Keys with the same zone set
     become one segment.
   - It renders each segment at a root inside the segment, mixing stereo L/R pairs and layers.
   - Looped samples are repeated through their loop for about 3 s, using a basic DAHDS volume
     envelope and a release fade.
   - Each preset is normalized to -1 dBFS.
   - The drum kit gets one file per key (61 keys).
   - Filters, modulators, LFOs and effects sends are not modelled.

   Output: `sf2/GeneralUser-GS-extracted/<NNN GM name>/k<root>.wav` plus `regions.json`.
2. `sfzlib.py`: an SFZ reader. It handles comments, `#define`/`#include`, `<control>`
   `default_path`/`set_ccN`, header inheritance across global/master/group/region, note names,
   and `key`/`lokey`/`hikey`/`pitch_keycenter`. It drops these regions:
   - `trigger=release` and `trigger=legato`
   - round robins (`seq_position>1`, `lorand>0`)
   - regions whose `loccN`/`hiccN` exclude the `set_cc` defaults

   For each key it picks the region covering velocity 100 (otherwise the nearest layer). It then
   merges equal neighbours and thins to 64 regions or fewer by keeping every Nth root and widening.
3. `build_index.py`: the curated SFZ list (names, families, articulations), plus GeneralUser.
   It writes `index.json` and verifies every file.

Re-run: `sf2_extract.py`, then `build_index.py`.

## Skipped or failed

- **sfizz VST3**: the zip doesn't contain one (see above).
- **VSCO `*-KS.sfz`**: keyswitch multis. Their articulations are indexed separately.
- **VSCO `.ariax` ensembles**: ARIA-only formats, not SFZ.
- **VSCO `Miscellania Raw/` and `VSCO 1 Percussion/`**: raw samples with no mapping.
- **Emilyguitar `emily_chords*`, `three_voice` and `five_voice`**: they rely on layering several
  regions per key (chords, unison voices), which the Sampler cannot do.
- **Emilyguitar `emily_basic`**: a duplicate of the clean program. The noise keys (90+) were left
  out of the clean guitar.
- **Meatbass**: only `02_arco_3vel` (arco) and `04_pizz` are indexed. `01_arco_modwheel` is a CC
  crossfade, `03_arco_5vel` duplicates arco, and `*_map.sfz` are include fragments.
- **Salamander `Retuned.sfz`**: an alternative tuning, not indexed.
- **Windows `tar.exe` (bsdtar)** hung on the `.tar.xz` with no CPU use. The archive was extracted
  with Python's `tarfile` instead.
- **Meatbass arco** uses `Samples/arco_looped` (the files `02_arco_3vel` maps). The source notes
  are only 2.3-4.7 s long, and the unlooped `Samples/arco` set is about the same length. In the
  Sampler, which plays once and does not loop, bowed bass notes stop after about 4 s.
