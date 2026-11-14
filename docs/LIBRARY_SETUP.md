# Installing the instrument library

The git repo has the app and the library's index files (`library/index.json`,
`library/surge_patches.json`, `library/obxf_patches.json`). It does not include the plug-ins
and samples those indexes point to, about 6 GB of third-party content. This guide puts each
one where Synthient looks for it.

All of it is free. Everything except the TAL plug-ins may be redistributed, but each piece is
still downloaded from its own project.

Synthient runs without the library: you get the Starter Kit drums, the built-in synth,
and any VST3 plug-ins you already have. Install only the parts you want. When you're done,
check what's there with:

```
python library\tools\check_library.py
```

It lists each part as `ok` or `MISSING`, with the first missing file, and works with any
Python 3.8 or later.

## Where the library goes

| How you run Synthient | Library folder |
|---|---|
| Built from source | `library\` in this repo (the app falls back to it) |
| Installed | `library\` next to `Synthient.exe` |
| Anywhere else | Put the folder path in `%APPDATA%\Synthient\library_path.txt` |

The final layout looks like this (folder names matter; the indexes use these exact paths):

```
library\
  plugins\
    Surge XT.vst3   Surge XT Effects.vst3   SurgeXTData\
    Dexed.vst3   OB-Xf.vst3   OB-Xf-assets\Surge Synth Team\OB-Xf\Patches\
    juicysfplugin.vst3   CHOWTapeModel.vst3   Odin2.vst3        (optional)
  sfz\
    VSCO-2-CE\   Salamander\   Meatbass\   Emilyguitar\   GeneralUser-GS\
  sf2\
    GeneralUser-GS.sf2   GeneralUser-GS-extracted\
```

## 1. Synth plug-ins

Synthient scans `library\plugins\` as well as `C:\Program Files\Common Files\VST3`, so a
normal installer works for every plug-in. Surge XT and OB-Xf also need their patch
folders inside `library\plugins\` (below), because Claude loads their patches from there.

| Plug-in | Get it from | Notes |
|---|---|---|
| **Surge XT** (main synth, ~2,900 patches) | <https://surge-synthesizer.github.io/> (tested with 1.3.4) | Required for Claude's synth sounds. See below. |
| **Dexed** (DX7 FM) | <https://github.com/asb2m10/dexed/releases> | `Dexed.vst3` |
| **OB-Xf** (Oberheim-style poly) | <https://github.com/surge-synthesizer/OB-Xf/releases> | See below. |
| juicysfplugin (SoundFont player) | <https://github.com/Birch-san/juicysfplugin/releases> | Optional |
| Chow Tape Model (tape saturation) | <https://github.com/jatinchowdhury18/AnalogTapeModel/releases> | Optional |
| Odin 2 (synth) | <https://thewavewarden.com/odin2> | Optional |

**Surge XT.** Synthient needs the factory data at `library\plugins\SurgeXTData\`, next to
`Surge XT.vst3`; it must contain `patches_factory\` and `patches_3rdparty\`. Use one of these:
- Put the `.vst3` bundles from Surge's Windows zip into `library\plugins\`, plus its data
  folder renamed to `SurgeXTData`.
- Run Surge's installer, then copy `C:\ProgramData\Surge XT` to
  `library\plugins\SurgeXTData`.

Surge looks for `SurgeXTData` in the folders above its own DLL, so keep the two side by side.

**OB-Xf.** Run its installer, or copy the plug-in to `library\plugins\OB-Xf.vst3`. For the
patch browser, copy OB-Xf's factory folder (with `Patches\` and `Themes\`) to
`library\plugins\OB-Xf-assets\Surge Synth Team\OB-Xf\`. If the plug-in itself can't find its
patches, copy the same folder to `%LOCALAPPDATA%\Surge Synth Team\OB-Xf`.

## 2. Sampled instruments (SFZ)

Played by Synthient's own SFZ engine; no plug-in needed.

| Library | Get it from | Put it in | Licence |
|---|---|---|---|
| **VSCO 2 Community Edition** (orchestra, 67 instruments) | <https://github.com/sgossner/VSCO-2-CE> (clone or "Download ZIP") | `sfz\VSCO-2-CE\` (the repo's root, with `SViolinVib.sfz` directly inside) | CC0 |
| **Salamander Grand Piano V3** | <https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html>: the file `SalamanderGrandPianoV3+20161209_44khz16bit.tar.xz` (use 7-Zip; Windows' `tar` can hang on it) | `sfz\Salamander\` (with `SalamanderGrandPianoV3.sfz` and `44.1khz16bit\` inside) | CC-BY 3.0 |
| **Karoryfer Meatbass** (upright bass) | <https://github.com/sfzinstruments/karoryfer.meatbass> | `sfz\Meatbass\` (with `Programs\02_arco_3vel.sfz` inside) | CC0 |
| **Karoryfer Emilyguitar** (electric guitar) | <https://github.com/sfzinstruments/karoryfer.emilyguitar> | `sfz\Emilyguitar\` (with `emily_clean.sfz` inside) | CC0 |

For the GitHub ones, use "Code > Download ZIP" (or `git clone`). Unpack each one, then rename its top folder to the name above. The checker shows the first
file it expects if a folder level is off.

Credit when you release music made with the piano: *Salamander Grand Piano by Alexander Holm, CC-BY 3.0*.

## 3. General MIDI sounds (GeneralUser GS)

All 128 General MIDI instruments and a drum kit, built from one SoundFont.

1. Download <https://github.com/mrbumpy409/GeneralUser-GS> ("Code > Download ZIP"; the
   project has no release files).
2. Copy `GeneralUser-GS.sf2` from it to `library\sf2\GeneralUser-GS.sf2`.
3. Convert it, with the analysis Python (`analysis\setup.cmd` creates it; it needs numpy and soundfile):

```
analysis\.venv\Scripts\python.exe library\tools\sf2_extract.py
analysis\.venv\Scripts\python.exe library\tools\sf2_to_sfz.py
```

This writes `sf2\GeneralUser-GS-extracted\` and `sfz\GeneralUser-GS\` (about 1 GB, a few
minutes). The licence allows any use, including commercial.

## 4. TAL plug-ins (optional)

TAL-NoiseMaker, TAL-Chorus-LX and TAL-Reverb-4 are free, but TAL's licence forbids
redistributing them, so get them from <https://tal-software.com/products>. Install them
normally, or put the `.vst3` files in this repo's `instruments\` folder (built from source)
or the `instruments\` folder next to `Synthient.exe` (installed).

## After installing

- Restart Synthient. New plug-ins appear after **Plug-ins > Options > Scan for new or updated
  VST3 plug-ins**.
- Run `python library\tools\check_library.py` again; it should end with "Everything required
  is installed."
- The indexes in the repo match the versions above. With newer versions some patches or
  samples may be renamed. The checker shows those, and you can rebuild the indexes with
  `library\tools\build_surge_index.py`, `build_obxf_index.py` and `build_index.py`.

More detail on how the library and its indexes were built: [library/NOTES.md](../library/NOTES.md).
