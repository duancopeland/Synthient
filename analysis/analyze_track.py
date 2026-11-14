"""analyze_track: musical analysis of one audio file for Synthient.

Prints exactly one JSON object to stdout. Progress and warnings go to stderr.
See README.md for the schema.
"""
from __future__ import annotations

import argparse
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# Keep model downloads (Demucs via Hugging Face / torch hub) inside analysis/models
os.environ.setdefault("TORCH_HOME", os.path.join(HERE, "models", "torch"))
os.environ.setdefault("HF_HUB_CACHE", os.path.join(HERE, "models", "hf"))  # demucs 4.1 pulls htdemucs from HF
os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")

import numpy as np  # noqa: E402

from daw_common import (  # noqa: E402
    PITCH_NAMES,
    Timer,
    chroma_for_key,
    db,
    emit,
    estimate_key,
    guard_stdout,
    load_audio,
    log,
)

SR = 22050
HOP = 512


# --------------------------------------------------------------------------- rhythm
def refine_tempo(onset_env: np.ndarray, sr: int, coarse: float) -> float:
    """Comb search: the BPM (within +-4% of librosa's estimate) whose beat grid, at its best phase,
    collects the most onset strength. Coarse 0.1 BPM pass, then 0.01 BPM around the winner."""
    fps = sr / HOP
    env = onset_env - onset_env.mean()
    env[env < 0] = 0
    n = len(env)
    frames = np.arange(n)

    def score(bpm: float) -> float:
        period = 60.0 * fps / bpm
        k = np.arange(0, int(n / period))
        pos = np.linspace(0, period, 48, endpoint=False)[:, None] + k[None, :] * period  # (phases, beats)
        vals = np.interp(pos.ravel(), frames, env, right=0.0).reshape(pos.shape)
        return float(vals.mean(axis=1).max())

    grid = np.arange(coarse * 0.96, coarse * 1.04, 0.1)
    best = max(grid, key=score)
    fine = np.arange(best - 0.12, best + 0.12, 0.01)
    bpm = float(max(fine, key=score))
    if abs(bpm - round(bpm)) < 0.12:  # most produced music sits on an integer BPM
        bpm = float(round(bpm))
    return bpm


def detect_rhythm(y: np.ndarray, sr: int):
    import librosa

    onset_env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=HOP, aggregate=np.median)
    coarse = float(np.atleast_1d(librosa.feature.tempo(onset_envelope=onset_env, sr=sr, hop_length=HOP))[0])
    # librosa's tempo comes from quantised tempogram bins (e.g. 117.45 instead of 120), and the beat
    # tracker then drifts. Refine with a fine comb search over period and phase on the onset envelope.
    tempo = refine_tempo(onset_env, sr, coarse)
    _, beats = librosa.beat.beat_track(onset_envelope=onset_env, sr=sr, hop_length=HOP, bpm=tempo,
                                       tightness=400, trim=False)
    beat_times = librosa.frames_to_time(beats, sr=sr, hop_length=HOP)
    if len(beat_times) >= 4:
        period = 60.0 / tempo
        # extend the grid backwards to the start of the file (the tracker often misses beat 1)
        bt = list(beat_times)
        while bt[0] - period > -0.04 * period:
            bt.insert(0, max(0.0, bt[0] - period))
        beat_times = np.array(bt)
        beats = librosa.time_to_frames(beat_times, sr=sr, hop_length=HOP)

    # Meter + downbeat: accent pattern of low-frequency onsets (kick) and harmonic change at beats
    low_env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=HOP, fmax=200, n_mels=32)
    chroma = librosa.feature.chroma_stft(y=y, sr=sr, hop_length=HOP)
    meter, downbeat_idx, meter_conf = 4, 0, 0.0
    if len(beats) >= 8:
        def at_beats(env):
            out = []
            for b in beats:
                lo, hi = max(b - 2, 0), min(b + 3, len(env))
                out.append(env[lo:hi].max() if hi > lo else 0.0)
            a = np.array(out)
            return (a - a.mean()) / (a.std() + 1e-9)

        acc_low = at_beats(low_env)
        # column i = median chroma of the span starting at beat i
        edges = list(beats) + [chroma.shape[1]]
        cs = np.stack([np.median(chroma[:, a:max(b, a + 1)], axis=1) for a, b in zip(edges[:-1], edges[1:])], axis=1)
        # harmonic novelty at each beat: two beats after vs two beats before (chords change at bar lines;
        # 2-beat windows smooth out bass-line movement inside the bar)
        nb_ = cs.shape[1]
        change = np.zeros(nb_)
        for i in range(nb_):
            pre = cs[:, max(0, i - 2):i].sum(axis=1)
            post = cs[:, i:min(nb_, i + 2)].sum(axis=1)
            if i >= 1:
                change[i] = 1 - np.dot(pre, post) / (np.linalg.norm(pre) * np.linalg.norm(post) + 1e-9)
        change = (change - change.mean()) / (change.std() + 1e-9)
        accent = 0.5 * acc_low + change

        best = {}
        for m in (3, 4):
            scores = []
            for p in range(m):
                on = accent[p::m]
                off = np.delete(accent, np.arange(p, len(accent), m))
                scores.append(on.mean() - off.mean())
            p = int(np.argmax(scores))
            best[m] = (scores[p], p)
        # strong prior for 4/4: 3/4 must clearly win
        if best[3][0] > 1.25 * best[4][0] and best[3][0] > 0.3:
            meter = 3
        meter_conf = float(np.clip(best[meter][0] / 2.0, 0, 1))
        downbeat_idx = best[meter][1]

    return {
        "tempo": tempo,
        "beat_times": beat_times,
        "meter": meter,
        "meter_conf": meter_conf,
        "downbeat_idx": downbeat_idx,
        "onset_env": onset_env,
    }


def time_to_beat_fn(beat_times: np.ndarray, tempo: float, downbeat_idx: int):
    """Returns f(t) -> beat position, with beat 0 = first downbeat. Linear extrapolation outside the grid."""
    bt = np.asarray(beat_times, dtype=float)
    period = 60.0 / tempo
    if len(bt) < 2:
        t0 = bt[0] if len(bt) else 0.0

        def f0(t):
            return (np.asarray(t) - t0) / period

        f0.beat_zero = float(t0)
        return f0
    idx = np.arange(len(bt), dtype=float)

    def f(t):
        t = np.asarray(t, dtype=float)
        b = np.interp(t, bt, idx)
        b = np.where(t < bt[0], (t - bt[0]) / period, b)
        b = np.where(t > bt[-1], len(bt) - 1 + (t - bt[-1]) / period, b)
        return b - downbeat_idx

    f.beat_zero = float(bt[min(downbeat_idx, len(bt) - 1)])
    return f


def bar_times(rh, duration: float) -> np.ndarray:
    bt = rh["beat_times"]
    m = rh["meter"]
    period = 60.0 / rh["tempo"]
    if len(bt) == 0:
        return np.arange(0, duration, period * m)
    downs = list(bt[rh["downbeat_idx"]:: m])
    # extend the bar grid to the end of the file
    while downs and downs[-1] + period * m < duration:
        downs.append(downs[-1] + period * m)
    # include a pickup bar starting at 0 if the first downbeat is late
    if downs and downs[0] > 0.5 * period * m:
        downs.insert(0, max(0.0, downs[0] - period * m))
    return np.array(downs)


# --------------------------------------------------------------------------- sections
def segment_sections(y, sr, bars: np.ndarray, duration: float, n_sections: int | None):
    """Bar-level checkerboard novelty (4-bar kernel) on timbre+harmony features, then label
    segments by similarity of their mean features (A, B, A, C...)."""
    import librosa

    bar_len = float(np.median(np.diff(bars))) if len(bars) > 1 else duration
    starts = [float(b) for b in bars]
    # a trailing partial bar (fade/tail) would look like its own section: fold it into the previous one
    if len(starts) > 1 and duration - starts[-1] < 0.75 * bar_len:
        starts.pop()
    nb = len(starts)
    if nb < 4:
        return [{"start_sec": 0.0, "end_sec": duration, "label": "A", "energy": 1.0, "bars": nb}]

    chroma = librosa.feature.chroma_cqt(y=y, sr=sr, hop_length=HOP, fmin=librosa.note_to_hz("C2"), n_octaves=6)
    mfcc = librosa.feature.mfcc(y=y, sr=sr, hop_length=HOP, n_mfcc=13)[1:]
    rms = librosa.feature.rms(y=y, hop_length=HOP)
    n = min(chroma.shape[1], mfcc.shape[1], rms.shape[1])
    edges = librosa.time_to_frames(starts + [min(duration, starts[-1] + bar_len)], sr=sr, hop_length=HOP)
    edges = np.clip(edges, 0, n)

    def bar_mean(M):
        return np.stack([M[:, a:max(b, a + 1)].mean(axis=1) for a, b in zip(edges[:-1], edges[1:])], axis=1)

    ch = bar_mean(chroma[:, :n])
    ch = ch / (ch.sum(axis=0, keepdims=True) + 1e-9)
    tim = bar_mean(mfcc[:, :n])
    en = np.log(bar_mean(rms[:, :n]) + 1e-5)

    def zs(M):
        return (M - M.mean(axis=1, keepdims=True)) / (M.std(axis=1, keepdims=True) + 1e-6)

    # each group contributes equally regardless of its dimensionality
    F = np.vstack([zs(ch) / np.sqrt(12), zs(tim) / np.sqrt(12), zs(en)])

    # checkerboard novelty: mean of the 4 bars before vs the 4 bars after each bar line
    W = 4
    nov = np.zeros(nb)
    # near the ends a short window is noisy, so require the full kernel unless the track is short
    lo_i, hi_i = (W, nb - W) if nb >= 3 * W else (2, nb - 2)
    for i in range(lo_i, hi_i + 1):
        w = min(W, i, nb - i)
        nov[i] = np.linalg.norm(F[:, i - w:i].mean(axis=1) - F[:, i:i + w].mean(axis=1))
    min_gap = 4 if nb >= 16 else 2
    cand = [i for i in range(1, nb) if nov[i] > 0 and nov[i] >= nov[max(0, i - 1)] and nov[i] >= nov[min(nb - 1, i + 1)]]
    cand.sort(key=lambda i: -nov[i])
    if n_sections:
        limit, thresh = n_sections - 1, 0.0
    else:
        limit = max(1, nb // 4)
        nz = nov[nov > 0]
        thresh = float(np.median(nz) + 0.5 * np.std(nz)) if len(nz) else np.inf
    bounds = []
    for i in cand:
        if len(bounds) >= limit or nov[i] < thresh:
            break
        if all(abs(i - b) >= min_gap for b in bounds) and i >= 2 and nb - i >= 2:
            bounds.append(i)
    bounds = [0] + sorted(bounds)
    seg_ranges = [(bounds[i], bounds[i + 1] if i + 1 < len(bounds) else nb) for i in range(len(bounds))]

    # labels: segments whose mean features are close share a letter
    means = np.array([F[:, a:b].mean(axis=1) for a, b in seg_ranges])
    within = np.mean([np.linalg.norm(F[:, i] - F[:, i + 1]) for i in range(nb - 1)])
    lab_thresh = 0.5 * within
    protos: list[np.ndarray] = []
    labels = []
    for mvec in means:
        d = [np.linalg.norm(mvec - p) for p in protos]
        if d and min(d) < lab_thresh:
            labels.append(int(np.argmin(d)))
        else:
            protos.append(mvec)
            labels.append(len(protos) - 1)
    log(f"[sections] bar boundaries {bounds} (novelty threshold {thresh:.2f}), labels {labels}")

    merged = []
    for (a, b), lab in zip(seg_ranges, labels):
        if merged and merged[-1][2] == lab:
            merged[-1][1] = b
        else:
            merged.append([a, b, lab])
    # relabel in order of first appearance after merging
    order: dict[int, int] = {}
    for m in merged:
        m[2] = order.setdefault(m[2], len(order))

    out = []
    for a, b, lab in merged:
        s = 0.0 if a == 0 else starts[a]
        e = starts[b] if b < nb else duration
        seg = y[int(s * sr): int(e * sr)]
        out.append({"start_sec": s, "end_sec": e, "label": _letter(lab), "bars": b - a,
                    "rms": float(np.sqrt(np.mean(seg ** 2))) if len(seg) else 0.0})
    peak = max(o["rms"] for o in out) or 1.0
    for o in out:
        o["energy"] = round(o.pop("rms") / peak, 3)
    return out


def _letter(i: int) -> str:
    s = ""
    i += 1
    while i:
        i, r = divmod(i - 1, 26)
        s = chr(65 + r) + s
    return s


def energy_curve(y, sr, bars: np.ndarray, duration: float):
    edges = list(bars) + [duration]
    rms_db, starts = [], []
    for a, b in zip(edges[:-1], edges[1:]):
        seg = y[int(a * sr): int(b * sr)]
        if len(seg) == 0:
            continue
        starts.append(float(a))
        rms_db.append(db(np.sqrt(np.mean(seg ** 2)), floor=-90))
    arr = np.array(rms_db)
    lo, hi = (arr.min(), arr.max()) if len(arr) else (0, 1)
    norm = (arr - lo) / (hi - lo) if hi > lo else np.ones_like(arr)
    return {"resolution": "bar", "bar_start_sec": starts, "rms_db": [round(v, 1) for v in rms_db],
            "energy": [round(float(v), 3) for v in norm]}


# --------------------------------------------------------------------------- stems (optional extra)
STEMS_NOT_INSTALLED = "Demucs not installed - run analysis\\setup.cmd --with-stems"


class StemsUnavailable(Exception):
    pass


def separate_stems(path: str, out_dir: str, tm: Timer):
    try:
        import torch
        from demucs.apply import apply_model
        from demucs.pretrained import get_model
    except Exception as e:  # noqa: BLE001
        log(f"demucs import failed: {e}")
        raise StemsUnavailable(STEMS_NOT_INSTALLED) from e
    import soundfile as sf

    device = "cuda" if torch.cuda.is_available() else "cpu"
    if device == "cpu":
        torch.set_num_threads(max(1, os.cpu_count() or 4))
    model = get_model("htdemucs")
    model.eval()
    tm.mark("demucs_model_load")
    wav, _ = load_audio(path, sr=model.samplerate, mono=False)
    if wav.shape[0] == 1:
        wav = np.repeat(wav, 2, axis=0)
    wav = wav[:2]
    t = torch.from_numpy(np.ascontiguousarray(wav))
    ref = t.mean(0)
    mean, std = ref.mean(), ref.std() + 1e-8
    with torch.no_grad():
        out = apply_model(model, ((t - mean) / std)[None], device=device, shifts=1, split=True,
                          overlap=0.25, progress=False)[0]
    out = (out * std + mean).cpu()
    os.makedirs(out_dir, exist_ok=True)
    stems = {}
    for name, src in zip(model.sources, out):
        p = os.path.join(out_dir, f"{name}.wav")
        a = src.numpy().T
        sf.write(p, a, model.samplerate, subtype="PCM_16")
        stems[name] = {"path": os.path.abspath(p), "rms_db": round(db(np.sqrt(np.mean(a ** 2)), -90), 1)}
    tm.mark("demucs_separate")
    return {"model": "htdemucs", "device": device, "sample_rate": model.samplerate, "stems": stems}


# --------------------------------------------------------------------------- transcription
# Basic Pitch settings per source. "mix" = the whole input file (often a user-exported stem).
BP_SETTINGS = {
    "mix": dict(minimum_frequency=None, maximum_frequency=None, minimum_note_length=70),
    "bass": dict(minimum_frequency=27.5, maximum_frequency=400.0, minimum_note_length=80),
    "other": dict(minimum_frequency=55.0, maximum_frequency=4200.0, minimum_note_length=60),
}


def _bp_model_path():
    from basic_pitch import FilenameSuffix, build_icassp_2022_model_path

    try:
        import onnxruntime  # noqa: F401 - lightest backend on Windows
        return build_icassp_2022_model_path(FilenameSuffix.onnx)
    except Exception:  # noqa: BLE001
        from basic_pitch import ICASSP_2022_MODEL_PATH
        return ICASSP_2022_MODEL_PATH


def transcribe(sources: dict, out_dir: str, to_beat, tempo: float, max_notes: int, grid: float, tm: Timer):
    """sources: {part_name: (audio_path, settings_key, midi_filename)}"""
    try:
        import pretty_midi
        from basic_pitch.inference import Model, predict
    except Exception as e:  # noqa: BLE001
        return {"error": f"basic-pitch not available: {e}"}
    model = Model(_bp_model_path())
    os.makedirs(out_dir, exist_ok=True)
    parts = {}
    for part, (audio_path, skey, mid_name) in sources.items():
        try:
            _, _, events = predict(audio_path, model, onset_threshold=0.5, frame_threshold=0.3,
                                   melodia_trick=True, midi_tempo=tempo, **BP_SETTINGS[skey])
            notes = []
            for ev in sorted(events, key=lambda e: (e[0], e[2])):
                s, e, pitch, amp = float(ev[0]), float(ev[1]), int(ev[2]), float(ev[3])
                sb, eb = float(to_beat(s)), float(to_beat(e))
                qs = round(sb / grid) * grid
                ql = max(grid, round((eb - sb) / grid) * grid)
                if qs < 0:
                    continue  # before the first downbeat (pickup)
                notes.append({"pitch": pitch, "start_beat": round(qs, 4), "length_beats": round(ql, 4),
                              "velocity": int(np.clip(round(amp * 127), 1, 127))})
            seen, uniq = set(), []  # collapse duplicates created by quantisation
            for n in notes:
                k = (n["pitch"], n["start_beat"])
                if k not in seen:
                    seen.add(k)
                    uniq.append(n)
            # .mid uses the same quantised grid as the JSON: beat 0 = first downbeat at the detected tempo
            pm = pretty_midi.PrettyMIDI(initial_tempo=tempo)
            inst = pretty_midi.Instrument(program=33 if part == "bass" else 0, name=part)
            spb = 60.0 / tempo
            for n in uniq:
                inst.notes.append(pretty_midi.Note(velocity=n["velocity"], pitch=n["pitch"],
                                                   start=n["start_beat"] * spb,
                                                   end=(n["start_beat"] + n["length_beats"]) * spb))
            pm.instruments.append(inst)
            mid_path = os.path.abspath(os.path.join(out_dir, mid_name))
            pm.write(mid_path)
            parts[part] = {"source_audio": os.path.abspath(audio_path), "midi_path": mid_path,
                           "total_notes": len(uniq), "notes_in_json": min(len(uniq), max_notes),
                           "truncated": len(uniq) > max_notes, "notes": uniq[:max_notes]}
        except Exception as e:  # noqa: BLE001
            log(traceback.format_exc())
            parts[part] = {"error": str(e)}
        tm.mark(f"basic_pitch_{part}")
    return {"engine": "basic-pitch (onnx)", "grid_beats": grid, "max_notes_per_part": max_notes,
            "beat_zero_sec": float(to_beat.beat_zero), "parts": parts}


# --------------------------------------------------------------------------- spectrogram
def save_spectrogram(y, sr, out_png: str, sections, title: str):
    import librosa
    import librosa.display
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    S = librosa.feature.melspectrogram(y=y, sr=sr, n_mels=128, hop_length=HOP, fmax=11025)
    Sdb = librosa.power_to_db(S, ref=np.max)
    fig, ax = plt.subplots(figsize=(14, 5), dpi=100)
    librosa.display.specshow(Sdb, sr=sr, hop_length=HOP, x_axis="time", y_axis="mel", fmax=11025, ax=ax,
                             cmap="magma")
    for s in sections:
        ax.axvline(s["start_sec"], color="cyan", lw=1.2, alpha=0.8)
        ax.text(s["start_sec"] + 0.2, 9000, s["label"], color="cyan", fontsize=12, weight="bold")
    ax.set_title(title)
    fig.tight_layout()
    os.makedirs(os.path.dirname(os.path.abspath(out_png)), exist_ok=True)
    fig.savefig(out_png)
    plt.close(fig)
    return os.path.abspath(out_png)


# --------------------------------------------------------------------------- summary
def fmt_t(s: float) -> str:
    return f"{int(s // 60)}:{int(s % 60):02d}"


def describe(res: dict, y, sr, perc_ratio: float) -> str:
    import librosa

    parts = []
    dur = res["duration_sec"]
    parts.append(f"{fmt_t(dur)} long, ~{res['tempo_bpm']:.0f} BPM in {res['time_signature']}")
    if res.get("key"):
        conf = res["key_confidence"]
        hedge = "" if conf >= 0.5 else (" (tentative)" if conf >= 0.25 else " (weak tonal center)")
        parts[-1] += f", key {res['key']}{hedge}"
    tempo = res["tempo_bpm"]
    feel = ("slow" if tempo < 85 else "mid-tempo" if tempo < 110 else "upbeat" if tempo < 135 else "fast")
    # timbre
    cent = float(np.median(librosa.feature.spectral_centroid(y=y, sr=sr)))
    tone = "dark" if cent < 1500 else "warm" if cent < 2500 else "bright"
    drums = ("drum-heavy" if perc_ratio > 0.35 else "with a clear beat" if perc_ratio > 0.15
             else "mostly sustained/ambient with little percussion")
    article = "An" if feel == "upbeat" else "A"
    s = f"{parts[0]}. {article} {feel}, {tone}-sounding track, {drums}."
    secs = res.get("sections", [])
    if len(secs) > 1:
        form = "".join(x["label"] for x in secs)
        s += f" Form {form}: " + "; ".join(
            f"{x['label']} {fmt_t(x['start_sec'])}-{fmt_t(x['end_sec'])} ({x['bars']} bars, "
            f"{'high' if x['energy'] > 0.8 else 'medium' if x['energy'] > 0.5 else 'low'} energy)" for x in secs) + "."
        en = [x["energy"] for x in secs]
        peak = int(np.argmax(en))
        s += f" Energy peaks in section {secs[peak]['label']} at {fmt_t(secs[peak]['start_sec'])}."
    elif secs:
        s += " Single section with no clear structural change (loop-like)."
    stems = (res.get("stems") or {}).get("stems")
    if stems:
        present = [k for k, v in stems.items() if v["rms_db"] > -40]
        s += " Stems with significant content: " + ", ".join(present) + "."
    parts = (res.get("transcription") or {}).get("parts") or {}
    for part, v in parts.items():
        notes = v.get("notes") or []
        if notes:
            pcs = [PITCH_NAMES[n["pitch"] % 12] for n in notes]
            common = sorted(set(pcs), key=pcs.count, reverse=True)[:4]
            lo, hi = min(n["pitch"] for n in notes), max(n["pitch"] for n in notes)
            label = "Whole-file transcription" if part == "mix" else f"{part.capitalize()} part"
            s += (f" {label}: {v['total_notes']} notes, range MIDI {lo}-{hi}, "
                  f"most used pitch classes {'/'.join(common)}.")
    return s


# --------------------------------------------------------------------------- main
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Analyze an audio file; prints one JSON object to stdout.")
    ap.add_argument("audio")
    ap.add_argument("--spectrogram", metavar="PNG", help="write a mel spectrogram PNG")
    ap.add_argument("--out-dir", help="where stems/MIDI go (default: <audio dir>/<name>_analysis)")
    ap.add_argument("--stems", action="store_true",
                    help="separate drums/bass/vocals/other with Demucs htdemucs (needs setup.cmd --with-stems)")
    ap.add_argument("--midi", action="store_true",
                    help="Basic Pitch transcription: of the bass+other stems with --stems, else of the whole file")
    ap.add_argument("--sections", type=int, default=None, help="force number of structural segments")
    ap.add_argument("--max-notes", type=int, default=2000, help="cap of notes per part in the JSON")
    ap.add_argument("--grid", type=float, default=0.25, help="note quantisation in beats (0.25 = 16th)")
    ap.add_argument("--max-beats", type=int, default=64, help="number of beat_times to include")
    args = ap.parse_args(argv)

    tm = Timer()
    path = os.path.abspath(args.audio)
    name = os.path.splitext(os.path.basename(path))[0]
    res: dict = {"file": path, "errors": []}
    if not os.path.isfile(path):
        emit({"file": path, "error": "file not found"})
        return 1
    try:
        y, sr = load_audio(path, sr=SR, mono=True)
    except Exception as e:  # noqa: BLE001
        emit({"file": path, "error": f"cannot read audio: {e}"})
        return 1
    if y.size == 0 or not np.any(y):
        emit({"file": path, "error": "audio is empty or silent"})
        return 1
    duration = len(y) / sr
    res["duration_sec"] = round(duration, 3)
    tm.mark("load")

    rh = detect_rhythm(y, sr)
    res["tempo_bpm"] = round(rh["tempo"], 2)
    res["beat_count"] = int(len(rh["beat_times"]))
    res["beat_times"] = [round(float(t), 3) for t in rh["beat_times"][: args.max_beats]]
    res["time_signature"] = f"{rh['meter']}/4"
    res["time_signature_confidence"] = round(rh["meter_conf"], 3)
    db_idx = rh["downbeat_idx"]
    res["first_downbeat_sec"] = round(float(rh["beat_times"][db_idx]), 3) if len(rh["beat_times"]) > db_idx else 0.0
    tm.mark("rhythm")

    import librosa

    y_harm, y_perc = librosa.effects.hpss(y)
    res_perc_ratio = float(np.sum(y_perc ** 2) / (np.sum(y ** 2) + 1e-12))
    tonic, mode, kconf = estimate_key(chroma_for_key(y, sr, y_harm=y_harm))
    del y_perc
    res["key"] = f"{tonic} {mode}" if tonic else None
    res["key_confidence"] = round(kconf, 3)
    tm.mark("key")

    bars = bar_times(rh, duration)
    res["bar_count"] = int(len(bars))
    try:
        res["sections"] = segment_sections(y, sr, bars, duration, args.sections)
    except Exception as e:  # noqa: BLE001
        log(traceback.format_exc())
        res["sections"] = [{"start_sec": 0.0, "end_sec": duration, "label": "A", "energy": 1.0, "bars": len(bars)}]
        res["errors"].append(f"sections: {e}")
    tm.mark("sections")
    res["energy_curve"] = energy_curve(y, sr, bars, duration)
    tm.mark("energy")

    out_dir = os.path.abspath(args.out_dir or os.path.join(os.path.dirname(path), name + "_analysis"))
    if args.stems:
        try:
            res["stems"] = separate_stems(path, out_dir, tm)
        except StemsUnavailable as e:
            res["stems_error"] = str(e)
        except Exception as e:  # noqa: BLE001
            log(traceback.format_exc())
            res["stems_error"] = f"stem separation failed: {e}"
        if "stems_error" in res:
            res["errors"].append("stems: " + res["stems_error"])

    if args.midi:
        stems = (res.get("stems") or {}).get("stems")
        if stems:
            sources = {p: (stems[p]["path"], p, f"{p}.mid") for p in ("bass", "other") if p in stems}
        else:
            sources = {"mix": (path, "mix", f"{name}.mid")}
        to_beat = time_to_beat_fn(rh["beat_times"], rh["tempo"], db_idx)
        tr = transcribe(sources, out_dir, to_beat, rh["tempo"], args.max_notes, args.grid, tm)
        res["transcription"] = tr
        if "error" in tr:
            res["errors"].append("transcription: " + tr["error"])
        for k, v in (tr.get("parts") or {}).items():
            if "error" in v:
                res["errors"].append(f"transcription.{k}: {v['error']}")

    if args.spectrogram:
        try:
            res["spectrogram_png"] = save_spectrogram(y, sr, args.spectrogram, res["sections"],
                                                      os.path.basename(path))
        except Exception as e:  # noqa: BLE001
            log(traceback.format_exc())
            res["errors"].append(f"spectrogram: {e}")
        tm.mark("spectrogram")

    res["summary"] = describe(res, y, sr, res_perc_ratio)
    tm.mark("summary")
    res["timings_sec"] = dict(tm.marks, total=tm.total())
    emit(res)
    return 0


if __name__ == "__main__":
    guard_stdout()
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception as e:  # noqa: BLE001
        log(traceback.format_exc())
        emit({"error": f"unexpected failure: {e}"})
        sys.exit(1)
