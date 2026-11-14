# GeneralUser GS to SFZ export

Script: `library/tools/sf2_to_sfz.py` (imports the SF2 parser from `sf2_extract.py`).
Run with `D:\ClaudeDAW\analysis\.venv\Scripts\python.exe` (the `python` on PATH has no soundfile).
`python sf2_to_sfz.py` converts everything; `python sf2_to_sfz.py 0 48 drums` converts a subset.

## Output

`library/sfz/GeneralUser-GS/`

- 129 SFZ files: `000 Acoustic Grand Piano.sfz` ... `127 Gunshot.sfz` (bank 0) plus
  `Drums - Standard Kit.sfz` (bank 128 preset 0, "Standard 1"). The names follow `sf2_extract.safe()`.
- `samples/`: 801 mono 16-bit WAVs, `<sample name>_<shdr index>.wav`. Only characters
  `[A-Za-z0-9._-]` are used, so there are no spaces. Each WAV holds the SF2 sample's start..end
  range (no padding). There is no smpl chunk; the loops are in the SFZ.
- 4215 regions in total. The samples are 29.5 MB of data. Drive D: has big clusters, so they take
  about 930 MB on disk (130 entries in the folder, 801 files in samples/).
- All 920 SF2 sample headers are mono (type 1). None is a stereo L/R pair, so the stereo code
  (pan -100/+100 added to the zone pan) is in the script but never runs for this bank.
- The script printed no warnings about skipped zones. The only warning: 164 regions span more
  than an octave and use keynumToVolEnvHold/Decay, which is approximated (see below).

`library/index.json` (backup in `index.json.bak`): still 200 instruments. All 129 GeneralUser
entries now have `"sfz": "sfz/GeneralUser-GS/<file>.sfz"` and the file exists. Their `regions`
(the 3 s renders in `sf2/GeneralUser-GS-extracted/`) are still there as a fallback for the
Tracktion sampler. `build_index.py` reports a problem if a GeneralUser SFZ is missing.

Self-check (`scratchpad/check_gu_sfz.py`, `--all` for all 129 files): 0 errors. Every sample
exists, every offset/loop point is inside its file, and every opcode is on the allowed list.

| file | regions | velocity layers | looped |
|---|---|---|---|
| 000 Acoustic Grand Piano | 200 | 8 | 200 |
| 048 String Ensemble 1 | 96 | 4 | 96 |
| 073 Flute | 48 | 4 | 48 |
| 019 Church Organ | 11 | 1 | 11 |
| Drums - Standard Kit | 175 | 67 distinct lovel/hivel pairs (per-key layers) | 44 |

## SFZ conventions the engine must follow

- `<control> default_path=samples/` (trailing slash). Each file starts with one `//` comment line.
  There are no `<group>` or `<global>` headers: each `<region>` carries all its opcodes on one line.
- Opcodes used: sample, lokey, hikey (`key=` is never written, since it also sets pitch_keycenter), lovel, hivel, pitch_keycenter, transpose, tune,
  pitch_keytrack, volume, pan, ampeg_delay/attack/hold/decay/sustain/release, loop_mode,
  loop_start, loop_end, offset, end, group, off_by, fil_type, cutoff, resonance. An opcode is
  written only when its value differs from the SFZ default.
- **Sample rates are mixed.** 801 files use about 100 different rates (8000 to 48000 Hz; 311 are
  44100). The engine must resample each region from the file's own rate.
- **loop_end and end are inclusive** (SFZ standard). SF2 endloop/end are exclusive, so
  `loop_end = endloop - start - 1`. loop_start/loop_end/offset count frames from the start of the
  WAV. Loop points are written only with loop_continuous/loop_sustain.
- loop_mode is always written: `no_loop` (SF2 modes 0/2, melodic), `one_shot` (modes 0/2, drum
  kit), `loop_continuous` (mode 1, 3700 regions), `loop_sustain` (mode 3, 17 regions). Looped
  drum sounds (44 regions, e.g. open hi-hat) keep their loop mode, and their envelope ends them.
- `offset` = startAddrsOffset (+32768 x coarse), on 542 regions. Loop offsets are folded into
  loop_start/loop_end. GeneralUser uses no end offsets, so no `end=` is written.
- Tuning: cents = coarse x 100 + fine + sample pitchCorrection, split into `transpose` (semitones)
  and `tune` (|tune| <= 50 cents). Root = overridingRootKey, else the sample's originalPitch.
  `pitch_keytrack` (scaleTuning) appears on 132 regions, for example 0 on some drums and FX.
- `volume` = -(initialAttenuation cB x 0.04) dB (FluidSynth/EMU 0.4 factor), always <= 0.
- `pan` = SF2 pan / 5, from -100 to 100.
- Envelope: seconds = 2^(timecents/1200). -12000 and below become 0 and are not written.
  `ampeg_sustain` = 100 x 10^(-cB/200) percent. SF2 decay is defined as the time to fall from
  full level to -100 dB. The engine should run decay/release in the dB (exponential) domain, or
  piano notes will die too fast. Contract: `ampeg_decay` here is the SF2 decay, i.e. seconds per
  100 dB of fall, and the phase stops at `ampeg_sustain`. An engine that uses standard SFZ
  time-to-sustain must scale it by `(-20*log10(sustain/100))/100`. Example: String Ensemble 1 has
  sustain 83% and decay 23.3 s, so it reaches sustain after about 0.4 s, not 23 s. Many pianos/plucks have a 15-30 s decay to sustain 0.001%.
  Release values reach 20 s on some drums: the engine needs a voice cap.
- `group=N off_by=N` from exclusiveClass: 27 regions, hi-hats in the kits.
- `fil_type=lpf_2p cutoff=<Hz> resonance=<dB>` when initialFilterFc < 13500 cents: 2853
  regions, including the low velocity layers of the piano. cutoff = 8.176 x 2^(cents/1200), and
  resonance = Q cB / 10 (918 regions have Q > 0).
- `lovel` is never 0: SF2 vel 0 is written as 1.

## Approximations / not modelled

- keynumToVolEnvHold/Decay is evaluated once, at the centre key of each region
  (`tc += (60 - centre) x amount`). This is exact for narrow regions; 164 regions span more than
  an octave and are approximate there.
- The SF2 default modulators are not modelled. Velocity to attenuation is left to the SFZ
  default amp_veltrack=100, so it is close but not the same curve. Velocity to filter cutoff
  (-2400 cents at low velocity) has no opcode, so it is dropped. Mod wheel to vibrato and the
  other default modulators are also dropped.
- Not converted: the modulation envelope, mod/vib LFOs (and their pitch/filter/volume targets),
  chorus/reverb sends, the custom pmod/imod modulators, the keynum/velocity override generators
  (GeneralUser does not use them), and 24-bit sm24 data (GeneralUser is 16-bit).
- Preset-level generators add to the instrument values for pan, attenuation, coarse/fine,
  scaleTuning, the six volume-envelope generators, filterFc and filterQ. Key and velocity ranges
  are intersected. Instrument-only generators are ignored at preset level. Results are clamped:
  attenuation 0..1440 cB, sustain 0..1440 cB, Fc 1500..13500, Q 0..960.
