"""Synthesize test material: a 120 BPM A-minor song (A/B/A form) and a folder of one-shots/loops.

Usage: python tests/make_test_audio.py [out_dir]   (default: tests/data)
"""
import os
import sys

import numpy as np
import soundfile as sf

SR = 44100
rng = np.random.default_rng(7)


def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def env_adsr(n, a=0.01, d=0.1, s=0.7, r=0.1):
    t = np.arange(n) / SR
    dur = n / SR
    e = np.where(t < a, t / max(a, 1e-6), np.where(t < a + d, 1 - (1 - s) * (t - a) / d, s))
    rel = np.clip((dur - t) / r, 0, 1)
    return e * rel


def saw(f, n, detune=0.0):
    t = np.arange(n) / SR
    out = 2 * ((t * f) % 1) - 1
    if detune:
        out = 0.5 * out + 0.5 * (2 * ((t * f * (1 + detune)) % 1) - 1)
    return out


def lowpass(x, cutoff):
    # one-pole lowpass, run twice
    a = np.exp(-2 * np.pi * cutoff / SR)
    from scipy.signal import lfilter
    for _ in range(2):
        x = lfilter([1 - a], [1, -a], x)
    return x


def highpass(x, cutoff):
    return x - lowpass(x, cutoff)


def kick(n=int(0.45 * SR)):
    t = np.arange(n) / SR
    f = 45 + 110 * np.exp(-t * 30)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return np.sin(ph) * np.exp(-t * 7) * 0.95


def snare(n=int(0.25 * SR)):
    t = np.arange(n) / SR
    noise = highpass(rng.standard_normal(n), 1200) * np.exp(-t * 18)
    tone = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 25)
    return 0.5 * noise + 0.5 * tone


def hat(n=int(0.07 * SR)):
    t = np.arange(n) / SR
    return highpass(rng.standard_normal(n), 7000) * np.exp(-t * 60) * 0.5


def clap(n=int(0.3 * SR)):
    t = np.arange(n) / SR
    e = np.zeros(n)
    for off in (0.0, 0.011, 0.022, 0.033):
        i = int(off * SR)
        e[i:] += np.exp(-(t[: n - i]) * (90 if off < 0.03 else 14))
    x = highpass(lowpass(rng.standard_normal(n), 3500), 900)
    return x * e * 0.4


def add(buf, x, start):
    i = int(start * SR)
    j = min(len(buf), i + len(x))
    if j > i:
        buf[i:j] += x[: j - i]


def chord_pad(notes, n):
    x = sum(saw(midi_hz(m), n, detune=0.004) for m in notes) / len(notes)
    return lowpass(x, 1800) * env_adsr(n, a=0.15, d=0.3, s=0.8, r=0.2)


def bass_note(m, n):
    return lowpass(saw(midi_hz(m), n), 500) * env_adsr(n, a=0.005, d=0.15, s=0.6, r=0.05)


def make_song(path, bpm=120.0):
    beat = 60.0 / bpm
    bar = 4 * beat
    # A: Am F C G   B: Dm Am E Am   (A natural/harmonic minor)
    prog_a = [(57, [57, 60, 64]), (53, [53, 57, 60]), (48, [48, 52, 55]), (55, [55, 59, 62])]
    prog_b = [(50, [50, 53, 57]), (57, [57, 60, 64]), (52, [52, 56, 59]), (57, [57, 60, 64])]
    form = [("A", prog_a), ("B", prog_b), ("A", prog_a)]
    total_bars = 8 * len(form)
    buf = np.zeros(int((total_bars * bar + 1.0) * SR))
    for si, (name, prog) in enumerate(form):
        for b in range(8):
            t0 = (si * 8 + b) * bar
            root, ch = prog[b % 4]
            add(buf, 0.18 * chord_pad([m + 12 for m in ch], int(bar * SR)), t0)
            # bass: root on 1, root on 2.5, fifth on 3, octave on 4 (8th-note groove)
            pattern = [(0, 0, 0.9), (1.5, 0, 0.4), (2, 7, 0.9), (3, 12, 0.9)]
            for pos, iv, ln in pattern:
                add(buf, 0.35 * bass_note(root - 12 + iv, int(ln * beat * SR)), t0 + pos * beat)
            for k in range(4):
                add(buf, kick(), t0 + k * beat)
            if name == "B":
                for k in (1, 3):
                    add(buf, 0.6 * snare(), t0 + k * beat)
                for k in range(8):
                    add(buf, 0.35 * hat(), t0 + k * beat / 2)
                # lead melody in B
                mel = [69, 72, 76, 74]
                for k, m in enumerate(mel):
                    add(buf, 0.12 * lowpass(saw(midi_hz(m + 12), int(0.9 * beat * SR)), 4000)
                        * env_adsr(int(0.9 * beat * SR), 0.01, 0.1, 0.6, 0.05), t0 + k * beat)
    buf /= np.max(np.abs(buf)) * 1.1
    sf.write(path, np.stack([buf, buf], axis=1).astype(np.float32), SR, subtype="PCM_16")


def make_samples(folder):
    os.makedirs(folder, exist_ok=True)
    w = lambda name, x: sf.write(os.path.join(folder, name), (x / (np.max(np.abs(x)) + 1e-9) * 0.9).astype(np.float32), SR)
    # neutral names: category must come from spectral features
    w("s01.wav", kick())
    w("s02.wav", snare())
    w("s03.wav", hat())
    w("s04.wav", clap())
    n = int(1.2 * SR)
    w("s05.wav", bass_note(33, n))                                # A1 bass one-shot
    n = int(3.0 * SR)
    w("s06.wav", chord_pad([57, 60, 64, 67], n))                  # Am7 pad
    n = int(0.6 * SR)
    t = np.arange(n) / SR
    w("s07.wav", lowpass(saw(midi_hz(64), n), 3000) * np.exp(-t * 6))  # E4 pluck
    # drum loop 2 bars @ 120
    beat = 0.5
    loop = np.zeros(int(8 * beat * SR))
    for k in range(8):
        add(loop, kick(), k * beat)
        add(loop, 0.4 * hat(), k * beat + beat / 2)
    for k in (1, 3, 5, 7):
        add(loop, 0.6 * snare(), k * beat)
    w("s08.wav", loop)
    # melodic loop 4 bars @ 100 BPM in E minor
    beat = 0.6
    loop = np.zeros(int(16 * beat * SR))
    seq = [64, 67, 71, 67, 62, 66, 69, 66, 60, 64, 67, 64, 59, 62, 66, 71]
    for k, m in enumerate(seq):
        add(loop, bass_note(m - 12, int(0.95 * beat * SR)) + 0.5 * chord_pad([m, m + 3 + (m % 2), m + 7], int(0.95 * beat * SR)), k * beat)
    w("s09.wav", loop)
    # keyword-named copies: filename heuristics
    os.makedirs(os.path.join(folder, "named"), exist_ok=True)
    sf.write(os.path.join(folder, "named", "Vox_Chop_Hey.wav"), 0.5 * clap(), SR)
    sf.write(os.path.join(folder, "named", "Riser FX 01.wav"), 0.3 * highpass(rng.standard_normal(2 * SR), 2000) * np.linspace(0, 1, 2 * SR), SR)
    sf.write(os.path.join(folder, "named", "Perc_Loop_128bpm.wav"), 0.5 * np.tile(np.r_[hat(), np.zeros(int(60 / 128 / 2 * SR) - len(hat()))], 16), SR)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "data")
    os.makedirs(out, exist_ok=True)
    make_song(os.path.join(out, "song_120_Am.wav"))
    make_samples(os.path.join(out, "samples"))
    print("wrote", out)
