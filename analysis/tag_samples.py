"""tag_samples: index a folder of audio samples for Synthient.

Prints one JSON object {"root", "count", "samples": [...], ...} to stdout; logs to stderr.
With --out, the same object is written to that file and reused for incremental updates
(files whose mtime and size are unchanged are not re-analysed).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import traceback
from concurrent.futures import ProcessPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402

from daw_common import AUDIO_EXTS, PITCH_NAMES, Timer, _clean as clean_json, db, emit, estimate_key, file_sig, guard_stdout, hz_to_note, load_audio, log  # noqa: E402

SR = 22050
MAX_SECONDS = 60.0  # analyse at most this much of very long files
INDEX_VERSION = 1
CATEGORIES = ["kick", "snare", "hat", "clap", "perc", "bass", "synth", "pad", "vocal", "fx", "other"]

# filename keyword -> category (checked in order; word-ish boundaries)
NAME_RULES = [
    ("kick", r"kick|kik|bd\d*\b|bassdrum|bass drum"),
    ("snare", r"snare|snr|\bsd\d*\b|rimshot|\brim\b"),
    ("clap", r"clap|clp|snap"),
    ("hat", r"hi-?hat|\bhh\b|\bhats?\b|\boh\b|\bch\b|open ?hat|closed ?hat|cymbal|ride|crash"),
    ("perc", r"perc|shaker|tamb|conga|bongo|tom\b|toms\b|cowbell|clave|wood ?block|triangle"),
    ("vocal", r"vox|vocal|voice|chant|acapella|adlib|\bvo\b"),
    ("fx", r"\bfx\b|sfx|riser|uplifter|downlifter|sweep|impact|whoosh|noise|transition|reverse|rev\b|boom"),
    ("bass", r"bass|\b808\b|sub\b|reese"),
    ("pad", r"\bpads?\b|atmos|ambient|drone|texture|strings"),
    ("synth", r"synth|lead|pluck|arp|chord|stab|keys|piano|bell|guitar|brass|melod"),
]
LOOP_WORDS = re.compile(r"loop|\d{2,3}\s*-?\s*bpm|\bbpm\b|groove|beat\b", re.I)
ONESHOT_WORDS = re.compile(r"one[ _-]?shot|\bhit\b|\bos\b|single", re.I)
BPM_IN_NAME = re.compile(r"(?<!\d)(\d{2,3})\s*-?\s*bpm|bpm\s*-?\s*(\d{2,3})(?!\d)", re.I)
DRUM_CATS = {"kick", "snare", "hat", "clap", "perc"}


def name_category(name: str) -> str | None:
    n = name.lower().replace("_", " ").replace("-", " ")
    for cat, pat in NAME_RULES:
        if re.search(pat, n):
            return cat
    return None


def features(y: np.ndarray, sr: int) -> dict:
    import librosa

    f: dict = {}
    n_fft = 2048 if len(y) >= 2048 else max(256, 1 << int(np.log2(max(len(y), 256))))
    S = np.abs(librosa.stft(y, n_fft=n_fft, hop_length=n_fft // 4))
    f["centroid"] = float(np.median(librosa.feature.spectral_centroid(S=S, sr=sr)))
    f["flatness"] = float(np.median(librosa.feature.spectral_flatness(S=S)))
    freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)
    power = (S ** 2).sum(axis=1)
    tot = power.sum() + 1e-12
    f["low_ratio"] = float(power[freqs < 150].sum() / tot)
    f["high_ratio"] = float(power[freqs > 5000].sum() / tot)
    f["body_ratio"] = float(power[(freqs > 120) & (freqs < 350)].sum() / tot)  # snare body vs clap
    _, yp = librosa.effects.hpss(y)
    f["perc_ratio"] = float(np.sum(yp ** 2) / (np.sum(y ** 2) + 1e-12))
    rms = librosa.feature.rms(y=y, frame_length=1024, hop_length=256)[0]
    peak_i = int(np.argmax(rms))
    f["attack_sec"] = peak_i * 256 / sr
    # time for the envelope to fall 20 dB below its peak (decay length)
    thr = rms[peak_i] * 0.1
    after = np.where(rms[peak_i:] < thr)[0]
    f["decay_sec"] = float(after[0] * 256 / sr) if len(after) else len(y) / sr
    # sustained-ness: share of frames within 12 dB of the peak
    f["sustain"] = float(np.mean(rms > rms[peak_i] * 0.25))
    onset_env = librosa.onset.onset_strength(y=y, sr=sr)
    onsets = librosa.onset.onset_detect(onset_envelope=onset_env, sr=sr, units="time", backtrack=False)
    f["onsets"] = onsets
    # pitch: harmonic, mostly monophonic content -> pyin; tonal-ness from voiced ratio and flatness
    f["f0"] = None
    f["voiced_ratio"] = 0.0
    if len(y) >= 2048:
        seg = y[: int(min(len(y), 4 * sr))]
        try:
            f0, voiced, _ = librosa.pyin(seg, fmin=30, fmax=2000, sr=sr, frame_length=2048)
            f["voiced_ratio"] = float(np.mean(voiced))
            if np.any(voiced):
                f["f0"] = float(np.nanmedian(f0[voiced]))
        except Exception:  # noqa: BLE001
            pass
    return f


def classify(name: str, dur: float, f: dict, kind: str) -> tuple[str, str]:
    """Returns (category, source) where source is 'filename' or 'audio'."""
    cat = name_category(name)
    if cat:
        return cat, "filename"
    percussive = f["perc_ratio"] > 0.3
    tonal = f["voiced_ratio"] > 0.35 and f["flatness"] < 0.05 and not percussive
    if kind == "loop":
        if percussive or (not tonal and len(f["onsets"]) >= 4):
            return "perc", "audio"  # drum/percussion loop
        if f["f0"] and f["f0"] < 160 and f["low_ratio"] > 0.3:
            return "bass", "audio"
        return ("pad" if f["sustain"] > 0.8 else "synth"), "audio"
    # one-shots. Kick first: a pitched sine sweep looks "tonal" to pyin but is short and sub-heavy.
    if dur < 1.0 and f["decay_sec"] < 0.6 and f["low_ratio"] > 0.6 and f["attack_sec"] < 0.05:
        return "kick", "audio"
    if f["f0"] and f["f0"] < 160 and f["low_ratio"] > 0.35 and tonal and dur > 0.3:
        return "bass", "audio"
    if tonal and f["voiced_ratio"] > 0.5:
        if dur >= 1.5 and f["attack_sec"] > 0.08 and f["sustain"] > 0.5:
            return "pad", "audio"
        return "synth", "audio"
    if dur < 1.2 and f["decay_sec"] < 0.8:
        if f["high_ratio"] > 0.5 or f["centroid"] > 6000:
            return "hat", "audio"
        if f["flatness"] > 0.08 and f["centroid"] > 1200:
            # snares have a tonal body around 150-300 Hz; claps are band-passed noise bursts
            return ("snare" if f["body_ratio"] > 0.15 else "clap"), "audio"
        return "perc", "audio"
    if f["flatness"] > 0.1 or (dur > 1.2 and not tonal):
        return "fx", "audio"
    return "other", "audio"


def loop_bpm(y, sr, dur: float, name: str) -> float | None:
    import librosa

    m = BPM_IN_NAME.search(name)
    if m:
        v = float(m.group(1) or m.group(2))
        if 50 <= v <= 220:
            return v
    tempo = float(np.atleast_1d(librosa.feature.tempo(y=y, sr=sr))[0])
    # loops are cut to whole bars: pick the 4/4 beat count whose exact BPM (from the duration)
    # is closest to the detected tempo
    best = None
    for beats in (4, 8, 16, 32, 64):
        bpm = 60.0 * beats / dur
        if 55 <= bpm <= 210:
            err = abs(np.log2(bpm / tempo))
            if best is None or err < best[0]:
                best = (err, bpm)
    if best and best[0] < 0.08:
        return round(best[1], 2)
    return round(tempo, 2)


def analyse_file(path: str) -> dict:
    import librosa

    name = os.path.basename(path)
    mtime, size = file_sig(path)
    entry = {"path": os.path.abspath(path), "name": name, "mtime": mtime, "size": size}
    try:
        import soundfile as sf

        info = sf.info(path)
        dur_full = float(info.duration)
    except Exception:  # noqa: BLE001
        dur_full = None
    y, sr = load_audio(path, sr=SR, mono=True, max_duration=MAX_SECONDS)
    if dur_full is None:
        dur_full = len(y) / sr
    entry["duration_sec"] = round(dur_full, 3)
    peak = float(np.max(np.abs(y))) if len(y) else 0.0
    entry["peak_db"] = round(db(peak), 2)
    entry["rms_db"] = round(db(np.sqrt(np.mean(y ** 2))) if len(y) else -120.0, 2)
    if peak < 1e-5:
        entry.update(kind="one-shot", category="other", category_source="audio", bpm=None, key=None)
        return entry
    # trim leading/trailing silence for analysis only
    yt, _ = librosa.effects.trim(y, top_db=50)
    if len(yt) < 512:
        yt = np.pad(yt, (0, 512 - len(yt)))
    dur = len(yt) / sr
    f = features(yt, sr)

    # kind: filename first, then rhythm evidence (several regular onsets spanning the file, sustained energy)
    if ONESHOT_WORDS.search(name):
        kind = "one-shot"
    elif LOOP_WORDS.search(name):
        kind = "loop"
    else:
        on = f["onsets"]
        regular = False
        if len(on) >= 4:
            d = np.diff(on)
            regular = np.std(d) / (np.mean(d) + 1e-9) < 0.6 or len(on) >= 8
        kind = "loop" if (dur_full >= 1.5 and regular and (on[-1] - on[0]) > 0.5 * dur) else "one-shot"
    entry["kind"] = kind

    cat, src = classify(name, dur_full, f, kind)
    entry["category"] = cat
    entry["category_source"] = src
    entry["bpm"] = loop_bpm(y, sr, dur_full, name) if kind == "loop" else None

    # key / pitch for tonal material
    tonal_cat = cat in {"bass", "synth", "pad", "vocal", "other"}
    entry["key"] = None
    entry["root_note"] = None
    pitched = f["flatness"] < 0.05 and f["voiced_ratio"] > 0.35  # noise (claps, risers) never gets a key
    if pitched and (tonal_cat and cat != "other" or (cat == "other" and f["voiced_ratio"] > 0.5)):
        if kind == "loop" or dur_full >= 2.0:
            chroma = librosa.feature.chroma_cqt(y=yt, sr=sr, fmin=librosa.note_to_hz("C1"), n_octaves=7)
            tonic, mode, conf = estimate_key(chroma.sum(axis=1))
            if tonic and conf >= 0.15:
                entry["key"] = f"{tonic} {mode}"
                entry["key_confidence"] = round(conf, 3)
        if f["f0"] and cat != "pad":  # pyin on a chord often locks onto a subharmonic
            entry["root_note"] = hz_to_note(f["f0"])
            if entry["key"] is None:
                # one-shot: report the pitch class as the "key" so it can be matched to a song key
                entry["key"] = PITCH_NAMES[int(round(69 + 12 * np.log2(f["f0"] / 440.0))) % 12]
    entry["features"] = {
        "centroid_hz": round(f["centroid"], 1), "flatness": round(f["flatness"], 4),
        "low_ratio": round(f["low_ratio"], 3), "high_ratio": round(f["high_ratio"], 3),
        "attack_sec": round(f["attack_sec"], 3), "decay_sec": round(f["decay_sec"], 3),
        "voiced_ratio": round(f["voiced_ratio"], 3), "onsets": int(len(f["onsets"])),
        "perc_ratio": round(f["perc_ratio"], 3), "body_ratio": round(f["body_ratio"], 3),
    }
    return entry


def _worker(path: str) -> dict:
    try:
        return analyse_file(path)
    except Exception as e:  # noqa: BLE001
        return {"path": os.path.abspath(path), "error": f"{type(e).__name__}: {e}",
                "trace": traceback.format_exc(limit=3)}


def _init_worker() -> None:
    # workers must never write to the real stdout (the JSON channel)
    try:
        os.dup2(2, 1)
    except OSError:
        pass
    sys.stdout = sys.stderr


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Tag a folder of audio samples; prints one JSON object.")
    ap.add_argument("folder")
    ap.add_argument("--out", help="also write the index here; an existing index is reused incrementally")
    ap.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 2) - 1)),
                    help="parallel worker processes (default: cores-1, max 8)")
    ap.add_argument("--no-recurse", action="store_true", help="only the top folder")
    ap.add_argument("--full", action="store_true", help="ignore the existing index and re-analyse everything")
    args = ap.parse_args(argv)

    tm = Timer()
    root = os.path.abspath(args.folder)
    if not os.path.isdir(root):
        emit({"root": root, "error": "folder not found", "samples": []})
        return 1

    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        for fn in sorted(filenames):
            if os.path.splitext(fn)[1].lower() in AUDIO_EXTS:
                files.append(os.path.join(dirpath, fn))
        if args.no_recurse:
            break

    old: dict[str, dict] = {}
    if args.out and os.path.isfile(args.out) and not args.full:
        try:
            with open(args.out, encoding="utf-8") as fh:
                prev = json.load(fh)
            if prev.get("index_version") == INDEX_VERSION:
                old = {e["path"]: e for e in prev.get("samples", []) if "path" in e}
        except Exception as e:  # noqa: BLE001
            log(f"ignoring unreadable index {args.out}: {e}")

    results: dict[str, dict] = {}
    todo = []
    for p in files:
        ap_ = os.path.abspath(p)
        prev = old.get(ap_)
        try:
            mtime, size = file_sig(p)
        except OSError:
            continue
        if prev and "error" not in prev and prev.get("mtime") == mtime and prev.get("size") == size:
            results[ap_] = prev
        else:
            todo.append(ap_)
    log(f"{len(files)} audio files, {len(results)} unchanged, {len(todo)} to analyse")

    errors = []
    if todo:
        if args.jobs > 1 and len(todo) > 40:  # worker start-up (imports + JIT) costs ~4 s each
            import tag_samples as _ts  # importable reference so spawned workers can unpickle it

            with ProcessPoolExecutor(max_workers=args.jobs, initializer=_ts._init_worker) as ex:
                futs = {ex.submit(_ts._worker, p): p for p in todo}
                for i, fut in enumerate(as_completed(futs), 1):
                    r = fut.result()
                    results[r["path"]] = r
                    if i % 25 == 0 or i == len(todo):
                        log(f"  {i}/{len(todo)}")
        else:
            for i, p in enumerate(todo, 1):
                r = _worker(p)
                results[r["path"]] = r
                log(f"  {i}/{len(todo)} {os.path.basename(p)}")
    samples = []
    for p in sorted(results):
        r = results[p]
        if "error" in r:
            errors.append({"path": r["path"], "error": r["error"]})
            log(r.get("trace", ""))
        else:
            samples.append(r)

    out = {
        "index_version": INDEX_VERSION,
        "root": root,
        "count": len(samples),
        "analysed": len(todo),
        "reused": len(files) - len(todo),
        "categories": {c: sum(1 for s in samples if s["category"] == c) for c in CATEGORIES},
        "samples": samples,
        "errors": errors,
        "elapsed_sec": tm.total(),
    }
    if args.out:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)
        out["index_path"] = os.path.abspath(args.out)
        tmp = args.out + ".tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(clean_json(out), fh, ensure_ascii=False, indent=1)  # same object as stdout
        os.replace(tmp, args.out)
    emit(out)
    return 0


if __name__ == "__main__":
    guard_stdout()
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception as e:  # noqa: BLE001
        log(traceback.format_exc())
        emit({"error": f"unexpected failure: {e}", "samples": []})
        sys.exit(1)
