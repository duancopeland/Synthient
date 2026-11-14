# Add-on plug-in downloads (research, 2026-11-03)

Research only; nothing was downloaded or installed. Several vendor pages returned HTTP 403 (valhalladsp.com) or gave partial info, so those items are marked "unconfirmed". Version info comes from pages as they appeared to a fetch tool and may lag newer releases.

## Summary table

| Plug-in | Latest version seen | Windows 64-bit package | VST3 | Size | Account/email | Type | Redistribute in our installer? |
|---|---|---|---|---|---|---|---|
| Odin 2 | 2.4.0 stable (site); 2.4.1 on GitHub "NightlyDevel" tag (dev build) | `https://github.com/TheWaveWarden/odin2/releases/download/NightlyDevel/Odin2.4.1WinInstaller.exe` (dev); stable site link is a Dropbox URL (below) | Yes (VST3, CLAP, LV2, AU per repo) | 40.9 MB (2.4.1 exe) | No (email signup optional, "No thanks, just download") | Inno-style installer .exe (silent switches unconfirmed) | Likely YES (open source, GPL-3.0 believed; license file not read, unconfirmed) |
| Chow Tape Model | 2.11.4 (2023-11-05) | `https://github.com/jatinchowdhury18/AnalogTapeModel/releases/download/v2.11.4/ChowTapeModel-Win-64bit-2.11.4.exe` | Yes (also VST, CLAP, AAX, LV2, standalone) | 10.6 MB | No | Installer .exe (silent switches unconfirmed) | YES, GPLv3 (confirmed on repo) |
| TAL-NoiseMaker | 5.0.6 (2023-08-16) | `https://tal-software.com/downloads/plugins/install_TAL-NoiseMaker.zip` (page gives relative link `../../downloads/plugins/...`) | Yes (also VST, AU, AAX, CLAP) | unknown | Not required per page (a login link exists) | Zip containing an installer (contents unconfirmed) | NO |
| TAL-Chorus-LX | 1.6.3 (2023-06-28) | `https://tal-software.com/downloads/plugins/install_TAL-Chorus-LX.zip` | Yes | unknown | Not required per page | Zip containing installer (unconfirmed) | NO |
| TAL-Reverb-4 | 4.0.4 (2023-08-16) | `https://tal-software.com/downloads/plugins/install_TAL-Reverb-4.zip` | Yes | unknown | Not required per page | Zip containing installer (unconfirmed) | NO |
| Valhalla Supermassive | 3.0.0 (2023-11-21) | Official page `https://valhalladsp.com/shop/reverb/valhalla-supermassive/` (also `https://valhalladsp.com/demos-downloads/`); exact file URL unconfirmed (403 to fetch tool) | Yes (VST3, VST2, AAX on Windows; 64-bit only) | ~2.9 MB zip (older 2.x figure, secondary source; unconfirmed for 3.0) | No registration or authorization needed (per search summaries) | Zip containing an .exe installer | NO (freeware EULA; EULA page was 403, text unconfirmed. Assume no redistribution; ask Valhalla for permission or link to the vendor page) |

## Per-plugin notes

### Odin 2 (TheWaveWarden)
- Official stable download: `https://thewavewarden.com/odin2` (not `/pages/odin2`, which is 404). Version 2.4.0, link `https://www.dropbox.com/scl/fi/ry724e80orl3sb2kam8h7/Odin2.4.0WinInstaller.exe?rlkey=eek7d00l6rbu9fzgpm93wgzdx&st=1sohdfiw&dl=1`. That Dropbox link carries a session-style token and may rotate, so it is fragile.
- GitHub releases only showed a "NightlyDevel" tag (2025-05-30, labelled devel/not stable, ships 2.4.1). Assets: Win installer 40.9 MB, Mac pkg 132 MB, Linux zip 66.6 MB, .deb. Prefer pinning to whatever version we test.
- Install location (default VST3 folder, admin needed): unconfirmed, expect `C:\Program Files\Common Files\VST3\Odin2.vst3`.
- License: open source on GitHub. GPL-3.0 is my belief, but the license file was not read. Verify before bundling.
- Presets: unconfirmed. Odin 2 uses its own `.odin` patch files (a preset browser reads a factory/user folder; exact Windows paths unconfirmed). Check the repo's `Odin2` source/manual before relying on this.

### Chow Tape Model (Chowdhury DSP)
- Release v2.11.4 on `jatinchowdhury18/AnalogTapeModel`, installer is the only Windows asset (10.6 MB, 70k downloads). Official site chowdsp.com also hosts it.
- License: GPLv3 (iOS app dual GPL/MPLv2). Redistribution inside our installer is allowed, subject to GPL terms (source offer, license text included).
- Install path and admin need: unconfirmed; typical for JUCE installers, `C:\Program Files\Common Files\VST3`, admin.
- Presets: ships factory presets; format and location unconfirmed (manual PDF on chowdsp.com).

### TAL-NoiseMaker (TAL Software)
- Page: `https://tal-software.com/products/tal-noisemaker`. v5.0.6. Description: virtual analog synth, 6 voices, 256 presets, free preset packs as .zip.
- License (from `https://tal-software.com/disclaimer`): "You may not share, resell, sublicense, rent, lease, or otherwise redistribute our products, whether modified or unmodified." Same terms cover free products. So do NOT bundle; link to the TAL download page and let the user install it (or have our installer fetch it only with TAL's okay).
- Presets: TAL plug-ins use XML-based preset files (NoiseMaker: `.noisemakerpreset`); location unconfirmed (historically under the TAL install/ProgramData/Documents folder). Unconfirmed from official source.

### TAL-Chorus-LX
- v1.6.3, VST3 for Windows and Linux. Same TAL license, no redistribution. Presets: `.cholxpreset`-style XML (unconfirmed).

### TAL-Reverb-4
- v4.0.4. Note from page: when upgrading, delete the old preset folder to get new presets, which implies a preset folder on disk (path unconfirmed). Same TAL license, no redistribution.

### Valhalla Supermassive
- Free, no authorization needed. v3.0.0 added Leo and Virgo modes (Nov 2023). Windows download is a zip containing an .exe installer (so not a plain-zip VST3). Valhalla's FAQ says to unzip, then run the installer.
- Default install path and presets: unconfirmed (Valhalla installers typically use `C:\Program Files\Common Files\VST3` and ship factory presets as vendor-format files; not verified).
- Redistribution: unconfirmed; freeware EULA, assume prohibited.

## Practical implications for Synthient
- Safe to bundle (GPL): Chow Tape Model; Odin 2 (verify license file).
- Not bundle-able (link or ask permission): all three TAL plug-ins, Valhalla Supermassive.
- For silent install, test each installer with `/VERYSILENT /SUPPRESSMSGBOXES` (works only if Inno Setup, unconfirmed) on a VM. TAL zips and Valhalla zips contain installers that must be unzipped first.

## Alternatives (warm analog / Juno / 80s, VST3 Windows, redistributable) - from my own knowledge, not verified this session
1. OB-Xf (Surge Synth Team): open-source Oberheim OB-X style polysynth, GPL. Already present in `D:\ClaudeDAW\library\plugins\OB-Xf-assets`.
2. Surge XT (Surge Synth Team): GPL-3 hybrid synth with analog-style oscillators and many filters.
3. Vital is NOT redistributable; instead consider Dexed/Helm. Helm (Matt Tytel) is GPL-3 and does warm subtractive pads; note Helm is unmaintained, VST3 build availability unconfirmed. Juno-specific free VST3s (e.g. TAL-U-No-LX, Hive-style) are freeware and not redistributable.
