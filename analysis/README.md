# Synthient analysis sidecar

Two command-line tools that the Synthient app runs as child processes:

- `analyze_track`: tempo, beats, meter, key, sections, energy curve, mel spectrogram PNG, a plain-language summary, and optional stems (Demucs) and audio-to-MIDI (Basic Pitch).
- `tag_samples`: indexes a sample folder (one-shot or loop, category, BPM, key, levels), with incremental updates.

Both print exactly **one JSON object to stdout**. All logs, progress and library warnings go to stderr. The tools route C-level stdout to stderr too, so libraries cannot corrupt the JSON channel.

## Setup

```
analysis\setup.cmd                 :: core: librosa + Basic Pitch (ONNX). ~545 MB venv
analysis\setup.cmd --with-stems    :: core + CPU torch + Demucs. ~1.2 GB venv + 81 MB model
```

`setup.cmd` deletes and recreates `analysis\.venv`. It picks a Python in this order:

1. `%CLAUDEDAW_PYTHON%`, if set.
2. Python 3.12 or 3.11 from the `py` launcher (including uv-managed `Astral/CPython3.12`).
3. Otherwise it installs `uv` with pip and provisions Python 3.12.

The system Python 3.14 is not used, because numba, onnxruntime and torch lack 3.14 wheels.

Pinned files:

- `requirements.txt`: core packages.
- `requirements-basic-pitch.txt`: installed with `--no-deps`. basic-pitch 0.4.0 declares `tensorflow<2.15.1`, which has no Python 3.12 wheel, so its bundled ONNX model runs on `onnxruntime` instead.
- `requirements-stems.txt`: CPU torch wheels and Demucs. If a CUDA torch build is installed in the venv instead, Demucs uses the GPU automatically.

The Demucs `htdemucs` weights (81 MB) download on first use to `analysis\models\hf\`. The tools set `HF_HUB_CACHE` and `TORCH_HOME` under `analysis\models`.

## Usage

```
analysis\run.cmd analyze_track <audio> --spectrogram <png> --out-dir <dir> [--stems] [--midi]
analysis\run.cmd tag_samples <folder> --out <index.json>
```

`run.cmd` works from any working directory, for example `cmd.exe /c D:\Synthient\analysis\run.cmd ...`. Exit code 0 means success. On a hard failure (unreadable file, missing venv) the exit code is non-zero and stdout is `{"error": "..."}`.

### analyze_track options

| flag | meaning |
|---|---|
| `--spectrogram PNG` | writes a 1400x500 mel spectrogram, with section boundaries and labels drawn on it |
| `--out-dir DIR` | where stems and MIDI files go. Default: `<audio dir>\<name>_analysis` |
| `--stems` | Demucs htdemucs separation into `DIR\drums.wav`, `bass.wav`, `vocals.wav` and `other.wav` (44.1 kHz, 16-bit). If Demucs is not installed, the normal analysis is still returned, plus `stems_error` |
| `--midi` | Basic Pitch transcription. With `--stems` it transcribes the bass and other stems (`DIR\bass.mid`, `DIR\other.mid`). Without `--stems` it transcribes the whole file (`DIR\<name>.mid`), which suits stems the user exported from their own separation tool |
| `--max-notes N` | cap on notes per part in the JSON (default 2000). The .mid file always has every note |
| `--grid 0.25` | quantisation of notes in beats (0.25 = a 16th note) |
| `--sections K` | force K structural segments (default: automatic) |
| `--max-beats 64` | how many `beat_times` to include |

Supported inputs are wav, flac, ogg and mp3 (decoded by libsndfile), plus aiff and m4a/opus where a decoder is available.

### analyze_track JSON

```jsonc
{
  "file": "D:\\...\\song.wav",
  "errors": [],                       // non-fatal problems (e.g. "stems: Demucs not installed ...")
  "duration_sec": 49.0,
  "tempo_bpm": 120.0,                 // comb-refined; snapped to an integer when within 0.12 BPM
  "beat_count": 96,
  "beat_times": [0.011, 0.511, ...],  // first --max-beats beats, seconds
  "time_signature": "4/4",            // "4/4" or "3/4" (4/4 is strongly preferred)
  "time_signature_confidence": 0.19,
  "first_downbeat_sec": 0.011,        // beat 0 for transcription
  "key": "A minor", "key_confidence": 0.56,   // Krumhansl-Kessler on harmonic chroma; 0..1
  "bar_count": 25,
  "sections": [                       // bar-aligned; same letter = similar material
    {"start_sec": 0.0, "end_sec": 16.02, "label": "A", "bars": 8, "energy": 0.977}, ...
  ],                                  // energy = section RMS / loudest section RMS
  "energy_curve": {"resolution": "bar", "bar_start_sec": [...], "rms_db": [...], "energy": [...]},  // energy 0..1 min-max
  "stems": {"model": "htdemucs", "device": "cpu", "sample_rate": 44100,
            "stems": {"drums": {"path": "...\\drums.wav", "rms_db": -21.3}, "bass": {...}, "vocals": {...}, "other": {...}}},
  "stems_error": "Demucs not installed - run analysis\\setup.cmd --with-stems",   // only when --stems failed
  "transcription": {
    "engine": "basic-pitch (onnx)", "grid_beats": 0.25, "max_notes_per_part": 2000, "beat_zero_sec": 0.011,
    "parts": {                        // "mix" (whole file) or "bass" + "other" (with --stems)
      "mix": {"source_audio": "...", "midi_path": "...\\song.mid", "total_notes": 501,
              "notes_in_json": 501, "truncated": false,
              "notes": [{"pitch": 45, "start_beat": 0.0, "length_beats": 1.0, "velocity": 92}, ...]}
    }
  },
  "spectrogram_png": "D:\\...\\song.png",
  "summary": "0:49 long, ~120 BPM in 4/4, key A minor. An upbeat, dark-sounding track, with a clear beat. Form ABA: ...",
  "timings_sec": {"load": 2.3, "rhythm": 5.0, "key": 3.2, "sections": 0.8, ..., "total": 11.4}
}
```

Notes are quantised to the detected beat grid. `start_beat` 0 is the first downbeat. Notes that start before it (a pickup) are dropped. Each .mid file uses the same grid and is written at the detected tempo, so it lines up with the song at beat 0. Velocity is Basic Pitch amplitude times 127.

### tag_samples options

| flag | meaning |
|---|---|
| `--out FILE` | also write the index to FILE (atomically). An existing FILE is reused, so only new or changed files (by mtime and size) are re-analysed |
| `--full` | ignore the existing index |
| `--jobs N` | worker processes for big batches (used only when more than 40 files need analysis; default is cores-1, at most 8) |
| `--no-recurse` | top folder only |

### tag_samples JSON

```jsonc
{
  "index_version": 1, "root": "D:\\Samples", "count": 12, "analysed": 1, "reused": 11,
  "categories": {"kick": 1, "snare": 1, "hat": 1, "clap": 1, "perc": 2, "bass": 1, "synth": 1, "pad": 2, "vocal": 1, "fx": 1, "other": 0},
  "samples": [
    {"path": "D:\\Samples\\s09.wav", "name": "s09.wav", "mtime": 1793391936.0, "size": 846764,
     "duration_sec": 9.6, "peak_db": -0.92, "rms_db": -10.54,
     "kind": "loop",                    // "one-shot" | "loop"
     "category": "pad",                 // kick|snare|hat|clap|perc|bass|synth|pad|vocal|fx|other
     "category_source": "audio",        // "filename" (keyword match) or "audio" (spectral heuristics)
     "bpm": 100.0,                      // loops only (BPM in the filename wins; else snapped to whole bars)
     "key": "E minor",                  // loops / long tonal samples: "X major|minor"; tonal one-shots: pitch class, e.g. "A"
     "key_confidence": 0.62,            // only when key came from the key profile
     "root_note": "A1",                 // tonal one-shots (not pads), from pyin
     "features": {"centroid_hz": 662.4, "flatness": 0.0, "low_ratio": 0.15, "high_ratio": 0.0,
                  "attack_sec": 4.83, "decay_sec": 0.55, "voiced_ratio": 1.0, "onsets": 17,
                  "perc_ratio": 0.006, "body_ratio": 0.82}}
  ],
  "errors": [{"path": "...", "error": "..."}],
  "elapsed_sec": 9.2,
  "index_path": "D:\\...\\index.json"   // only with --out (not stored in the file)
}
```

The category comes from filename keywords first (kick/bd, snare/sd/rim, clap, hh/hat/cymbal, perc/shaker/tom, vox/vocal, fx/riser/impact, bass/808/sub, pad/atmos/drone, synth/lead/pluck/keys/chord). Without a keyword, rules on the audio decide:

- **kick**: a short, sub-heavy hit with a fast attack.
- **hat**: more than 50% of the energy above 5 kHz.
- **snare or clap**: noisy midrange. A tonal body at 120 to 350 Hz means snare, otherwise clap.
- **bass, synth or pad**: pitched material, told apart by pitch and envelope.
- **perc (loop)**: an HPSS percussive ratio above 0.3.

## Timings

Measured on this machine (Windows 10, CPU only, warm caches). The test file is a 49 s WAV.

| run | wall time |
|---|---|
| `analyze_track` core + `--spectrogram` | ~7-12 s (2-4 s of that is interpreter start-up and imports) |
| `analyze_track` + `--midi` (whole file, Basic Pitch ONNX) | ~8-9 s (Basic Pitch itself takes ~1 s) |
| `analyze_track` + `--stems --midi` | ~31-34 s warm: Demucs model load 2.5-3 s, separation 20-21 s, Basic Pitch on bass + other 1.4 s |
| first-ever `--stems` run | ~40 s, including the 80 MB htdemucs download to `models\hf` |
| `analyze_track` 60 s MP3, core | ~28 s (measured while another build was running) |
| `tag_samples` 12 files, cold | ~10-14 s |
| `tag_samples` 12 files, all unchanged | <1 s |

Rough scaling:

- Core analysis: about 0.15x real time.
- Demucs on CPU: about 0.45x real time on an idle machine. On a busy machine it was up to 3x real time (156 s for 49 s).

## Tests

`.venv\Scripts\python tests\make_test_audio.py` synthesises:

- `tests\data\song_120_Am.wav`: 120 BPM, A minor, form A (Am F C G) / B (Dm Am E Am with drums and a lead) / A.
- A sample folder with neutral filenames, so the audio heuristics get exercised.
