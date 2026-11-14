"""Shared helpers for the Synthient analysis sidecar (key detection, IO, JSON output)."""
from __future__ import annotations

import json
import os
import sys
import time
import warnings

import numpy as np

warnings.filterwarnings("ignore")

AUDIO_EXTS = {".wav", ".mp3", ".flac", ".ogg", ".aif", ".aiff", ".m4a", ".opus"}
PITCH_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# Krumhansl-Kessler key profiles
_MAJOR = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
_MINOR = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def log(*args) -> None:
    """Progress/diagnostics go to stderr only; stdout is reserved for the JSON result."""
    print(*args, file=sys.stderr, flush=True)


class Timer:
    def __init__(self):
        self.t0 = time.perf_counter()
        self.marks: dict[str, float] = {}
        self._last = self.t0

    def mark(self, name: str) -> None:
        now = time.perf_counter()
        self.marks[name] = round(now - self._last, 3)
        self._last = now
        log(f"[{now - self.t0:7.2f}s] {name} done ({self.marks[name]:.2f}s)")

    def total(self) -> float:
        return round(time.perf_counter() - self.t0, 3)


_REAL_STDOUT_FD: int | None = None


def guard_stdout() -> None:
    """Route everything libraries print (Python-level and C-level fd 1) to stderr, keeping a private
    handle on the real stdout for the single JSON result."""
    global _REAL_STDOUT_FD
    if _REAL_STDOUT_FD is not None:
        return
    sys.stdout.flush()
    _REAL_STDOUT_FD = os.dup(1)
    os.dup2(2, 1)
    sys.stdout = sys.stderr


def emit(obj) -> None:
    """Write exactly one JSON object to the real stdout (UTF-8, no NaN)."""
    data = json.dumps(_clean(obj), ensure_ascii=False).encode("utf-8") + b"\n"
    if _REAL_STDOUT_FD is not None:
        view = memoryview(data)
        while view:
            view = view[os.write(_REAL_STDOUT_FD, view):]
    else:
        sys.stdout.buffer.write(data)
        sys.stdout.flush()


def _clean(o):
    if isinstance(o, dict):
        return {str(k): _clean(v) for k, v in o.items()}
    if isinstance(o, (list, tuple)):
        return [_clean(v) for v in o]
    if isinstance(o, np.ndarray):
        return _clean(o.tolist())
    if isinstance(o, (np.floating, float)):
        f = float(o)
        return None if (np.isnan(f) or np.isinf(f)) else round(f, 4)
    if isinstance(o, np.integer):
        return int(o)
    if isinstance(o, np.bool_):
        return bool(o)
    return o


def load_audio(path: str, sr: int = 22050, mono: bool = True, max_duration: float | None = None):
    """Load audio. soundfile first (wav/flac/ogg/mp3 via libsndfile), librosa/audioread fallback."""
    import librosa

    y, file_sr = None, None
    try:
        import soundfile as sf

        with sf.SoundFile(path) as f:
            file_sr = f.samplerate
            frames = -1 if max_duration is None else int(max_duration * file_sr)
            y = f.read(frames=frames, dtype="float32", always_2d=True).T  # (ch, n)
    except Exception:
        y, file_sr = librosa.load(path, sr=None, mono=False, duration=max_duration)
        y = np.atleast_2d(y).astype(np.float32)
    if mono:
        y = y.mean(axis=0)
    if sr is not None and file_sr != sr:
        y = librosa.resample(y, orig_sr=file_sr, target_sr=sr, res_type="soxr_hq")
        file_sr = sr
    return y, file_sr


def estimate_key(chroma_vec: np.ndarray):
    """Krumhansl-Schmuckler. chroma_vec: 12-dim summed chroma.
    Returns (tonic_name, mode, confidence 0..1, scores)."""
    c = np.asarray(chroma_vec, dtype=float)
    if c.sum() <= 0 or np.allclose(c, c[0]):
        return None, None, 0.0
    scores = []
    for mode, prof in (("major", _MAJOR), ("minor", _MINOR)):
        for tonic in range(12):
            r = np.corrcoef(c, np.roll(prof, tonic))[0, 1]
            scores.append((r, tonic, mode))
    scores.sort(reverse=True)
    best, second = scores[0], scores[1]
    # confidence: combination of absolute correlation and margin over the runner-up
    conf = float(np.clip(0.5 * max(best[0], 0) + 2.5 * (best[0] - second[0]), 0, 1))
    return PITCH_NAMES[best[1]], best[2], conf


def chroma_for_key(y: np.ndarray, sr: int, y_harm: np.ndarray | None = None) -> np.ndarray:
    import librosa

    # Harmonic part only so drums do not smear the chroma
    yh = y_harm if y_harm is not None else librosa.effects.harmonic(y, margin=2.0)
    # fmin = C2 (65 Hz): ignore sub-bass, where kick-drum tails ring at ~40-60 Hz and fake a pitch class
    chroma = librosa.feature.chroma_cqt(y=yh, sr=sr, hop_length=1024, fmin=librosa.note_to_hz("C2"), n_octaves=6)
    # weight frames by energy
    return chroma.sum(axis=1)


def hz_to_note(hz: float) -> str | None:
    if not hz or hz <= 0 or np.isnan(hz):
        return None
    midi = int(round(69 + 12 * np.log2(hz / 440.0)))
    return f"{PITCH_NAMES[midi % 12]}{midi // 12 - 1}"


def db(x: float, floor: float = -120.0) -> float:
    return float(max(20 * np.log10(max(float(x), 1e-12)), floor))


def file_sig(path: str) -> tuple[float, int]:
    st = os.stat(path)
    return round(st.st_mtime, 3), int(st.st_size)
