# Analysis sidecar: results notes (2026-10-30)

## Command lines the C++ app invokes

```
cmd.exe /c <abs>\analysis\run.cmd analyze_track <audio path> --spectrogram <png path> --out-dir <dir> [--stems] [--midi]
cmd.exe /c <abs>\analysis\run.cmd tag_samples <folder> --out <index.json>
```

- stdout carries exactly one JSON object. Python output and C-level fd 1 are redirected to stderr inside the tools.
- `run.cmd` works from any working directory, and paths with spaces were tested.
- On a hard failure stdout is `{"error": ...}` with a non-zero exit code.
- If the venv is missing, `run.cmd` prints `{"error": "analysis venv missing - run analysis/setup.cmd"}` and exits with code 2.

### analyze_track: stems and MIDI behaviour

| flags | stems | MIDI |
|---|---|---|
| neither | none | none |
| `--midi` | none | Basic Pitch on the whole file, written to `<dir>\<name>.mid`. JSON: `transcription.parts.mix` |
| `--stems` | Demucs htdemucs writes `<dir>\drums.wav`, `bass.wav`, `vocals.wav`, `other.wav`. JSON: `stems.stems.<name>.path` | none |
| `--stems --midi` | as above | bass and other stems, written to `<dir>\bass.mid` and `<dir>\other.mid`. JSON: `transcription.parts.bass` and `.other` |
| `--stems` without Demucs installed | the normal analysis is still returned, plus `"stems_error": "Demucs not installed - run analysis\\setup.cmd --with-stems"`. With `--midi`, it falls back to whole-file transcription | |

Notes are quantised to a 1/16 grid. Beat 0 is the first downbeat.

- Each part holds up to 2000 notes (`--max-notes`). The JSON reports the cap as `max_notes_per_part`, plus `truncated` and `total_notes` per part.
- The .mid files use the same quantised grid at the detected tempo, and they contain every note.

Full JSON schemas are in README.md.

## What works (verified on synthetic audio)

### Song test

The test song is 120 BPM, A minor, 4/4, with the form A (Am F C G) / B (Dm Am E Am, adding drums and a lead) / A, 49 s long.

| feature | result |
|---|---|
| tempo | 120.0 |
| key | A minor (confidence 0.56) |
| time signature | 4/4 |
| first downbeat | 0.011 s, correct |
| sections | A 0-16 s, B 16-32 s, A 32-49 s. Exact bar boundaries and correct repeat labelling |
| `--midi` on the whole mix | 501 notes |

With `--stems --midi`, the bass transcription reproduces the programmed line exactly: A2 on beat 0, A2 on 1.5, E3 on 2, A3 on 3, then F2... (105 notes).

The same song rendered at 97 BPM and encoded to MP3 gave a tempo of 97.0, A minor, and the form ABA.

### tag_samples test

The test set was 12 samples, 9 of them with neutral filenames (s01-s09), so the audio heuristics had to decide. All 9 were classified correctly:

- kick, snare, hat and clap.
- bass one-shot A1, giving key "A".
- Am7 pad, giving "A minor".
- E4 pluck, giving synth "E".
- 2-bar drum loop: perc at 120 BPM.
- 4-bar E-minor loop: pad at 100 BPM, "E minor".

The 3 keyword-named files were classified by their filenames.

Incremental mode works: with one file touched, 1 was analysed and 11 reused, and with no changes the run took under 1 s. The `--out` file holds the same object as stdout.

## Fixes made along the way

| problem | fix |
|---|---|
| librosa's tempo is quantised (117.45 instead of 120) and the beat tracker drifts | fine comb search over BPM and phase, then `beat_track(bpm=..., tightness=400)` |
| kick-drum sine tails at about 45 Hz faked an F# tonic | key chroma starts at C2 (65 Hz) |
| downbeat detection | low-band onset accent plus 2-beat harmonic novelty |
| `librosa.segment.agglomerative` missed the sections | replaced with a 4-bar checkerboard novelty on bar-level chroma, MFCC and RMS. Segments are labelled by mean-feature distance |
| basic-pitch printed to stdout and corrupted the JSON | the stdout guard routes it to stderr |
| Demucs failed to import in the fresh venv: `No module named 'pkg_resources'` | pinned `setuptools==80.10.2` in requirements-stems.txt |

## What failed or was worked around

- **System Python 3.14 is unusable** for numba, onnxruntime and torch. The venv uses the uv-managed CPython 3.12.13 that was already installed (found through `py -V:Astral/CPython3.12`). `setup.cmd` falls back to `uv python install 3.12`.
- **basic-pitch 0.4.0 declares `tensorflow<2.15.1`**, which has no Python 3.12 wheel. It is installed with `--no-deps` from `requirements-basic-pitch.txt`, and runs its bundled `nmp.onnx` through onnxruntime. TensorFlow is not installed. Harmless "Tensorflow/CoreML not installed" warnings appear on stderr.
- **Demucs 4.1.0 downloads htdemucs from Hugging Face**, not torch hub. `HF_HUB_CACHE` is set to `analysis\models\hf`.
- The heuristics are tuned on synthetic material. Section labelling and the 3/4 detection are the least proven on real music. `time_signature_confidence` is low (about 0.2) even when the answer is right.

## Install size

Sizes are apparent sizes from `du --apparent-size`. Plain `du` reported 15 GB for the venv because of how this drive allocates space, so it is not meaningful here.

| part | size |
|---|---|
| core venv (librosa + Basic Pitch ONNX) | ~545 MB |
| core + stems venv (+ torch 2.15.0+cpu, torchaudio, demucs) | ~1.2 GB |
| htdemucs model in `analysis\models\hf` | 81 MB |
| `setup.cmd --with-stems` from cached wheels | several minutes on this disk (most of it is deleting the old venv and extracting files; exact time not measured) |

## Runtimes

Measured on CPU (no CUDA), using the 49 s test WAV and calling through `run.cmd` from `C:\`.

| run | wall time | internal breakdown |
|---|---|---|
| core + spectrogram + `--midi` (whole file) | 9 s | load 0.9, rhythm 2.6, key 2.2, sections 0.4, Basic Pitch 0.9, PNG 0.8 |
| `--stems --midi`, first run with model download | 40 s | Demucs load 9.9, separate 21.1 |
| `--stems --midi`, warm | 31-34 s | Demucs load 2.5-3, separate 20-21, Basic Pitch 1.4 |
| `tag_samples`, 12 files cold | 10-14 s | |
| `tag_samples`, all unchanged | under 1 s | |

An earlier run of Demucs, while the machine was busy with a C++ build, took 156 s to separate 49 s of audio. Expect a range of 0.4x to 3x real time on CPU.

## Repo housekeeping (not done: .gitignore is outside this folder)

Add these lines to `D:\Synthient\.gitignore`:

```
analysis/.venv/
analysis/models/
analysis/tests/data/
**/__pycache__/
```

`tests/data` is regenerated by `tests/make_test_audio.py`.

## Files

| file | purpose |
|---|---|
| `analyze_track.py`, `tag_samples.py` | the tools |
| `daw_common.py` | key detection, audio IO, stdout guard |
| `cli.py` | dispatcher used by `run.cmd` |
| `run.cmd`, `setup.cmd` | wrappers |
| `requirements.txt`, `requirements-basic-pitch.txt`, `requirements-stems.txt` | pinned dependencies |
| `tests/make_test_audio.py` | synthesises the test audio |
| `README.md` | setup, usage, schemas |
