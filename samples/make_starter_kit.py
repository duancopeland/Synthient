"""Synthesizes ClaudeDAW's built-in Starter Kit (drum one-shots) into samples/Starter Kit/.

Run with the analysis venv: analysis\\.venv\\Scripts\\python.exe samples\\make_starter_kit.py
Every sound is generated from scratch (no third-party audio), so the kit can ship freely.
"""
import os
import wave

import numpy as np

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Starter Kit")
rng = np.random.default_rng(7)


def t(seconds):
    return np.arange(int(SR * seconds)) / SR


def env(length, attack=0.001, decay=0.2, curve=1.0):
    x = t(length)
    a = np.clip(x / max(attack, 1e-6), 0, 1)
    d = np.exp(-x / max(decay, 1e-6)) ** curve
    return a * d


def sweep(f0, f1, length, speed):
    x = t(length)
    freq = f1 + (f0 - f1) * np.exp(-x / speed)
    return np.sin(2 * np.pi * np.cumsum(freq) / SR)


def noise(length):
    return rng.uniform(-1, 1, len(t(length)))


def onepole_hp(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    y = np.zeros_like(x)
    prev_x = prev_y = 0.0
    for i, s in enumerate(x):
        prev_y = a * (prev_y + s - prev_x)
        prev_x = s
        y[i] = prev_y
    return y


def onepole_lp(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    y = np.zeros_like(x)
    prev = 0.0
    for i, s in enumerate(x):
        prev = (1 - a) * s + a * prev
        y[i] = prev
    return y


def bandpass(x, low, high):
    return onepole_lp(onepole_hp(x, low), high)


def saturate(x, drive=1.5):
    return np.tanh(x * drive) / np.tanh(drive)


def metallic(length):
    # Six detuned square waves at inharmonic ratios (classic cymbal recipe).
    x = t(length)
    ratios = [2.0, 3.0, 4.16, 5.43, 6.79, 8.21]
    return sum(np.sign(np.sin(2 * np.pi * 330 * r * x)) for r in ratios) / len(ratios)


def reverb_tail(x, length=0.9, decay=0.25):
    ir = noise(length) * env(length, 0.002, decay)
    wet = np.convolve(x, ir)[: len(x) + len(ir)]
    return wet / (np.max(np.abs(wet)) + 1e-9)


def write(name, x, peak_db=-1.0):
    x = np.asarray(x, dtype=np.float64)
    fade = min(len(x), int(0.004 * SR))
    x[-fade:] *= np.linspace(1, 0, fade)
    x = x / (np.max(np.abs(x)) + 1e-9) * (10 ** (peak_db / 20))
    pcm = (x * 32767).astype("<i2")
    os.makedirs(OUT, exist_ok=True)
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def main():
    # Kicks
    click = np.concatenate([noise(0.004) * 0.6, np.zeros(len(t(0.4)) - len(t(0.004)))])
    write("Kick - DnB punchy", saturate(sweep(220, 52, 0.4, 0.03) * env(0.4, 0.0005, 0.12) + click, 2.0))
    write("Kick - Synthwave round", saturate(sweep(160, 48, 0.55, 0.04) * env(0.55, 0.0008, 0.2), 1.3))
    write("Kick - 808 long", sweep(110, 44, 1.4, 0.06) * env(1.4, 0.001, 0.55))

    # Snares
    body = sweep(320, 190, 0.25, 0.02) * env(0.25, 0.0005, 0.06)
    snap = bandpass(noise(0.25), 1500, 9000) * env(0.25, 0.0005, 0.07)
    write("Snare - DnB tight", saturate(body * 0.7 + snap, 1.6))

    hit = sweep(260, 180, 0.6, 0.03) * env(0.6, 0.0005, 0.08) * 0.6 + bandpass(noise(0.6), 900, 8000) * env(0.6, 0.0005, 0.1)
    gated = reverb_tail(hit, 0.8, 0.4)[: len(t(0.42))]
    gated *= np.where(t(0.42) < 0.36, 1.0, np.linspace(1, 0, len(t(0.42)))[: len(gated)])
    write("Snare - 80s gated", saturate(gated * 0.8 + np.pad(hit[: len(gated)], (0, 0)) * 0.5, 1.4))

    write("Rim", bandpass(noise(0.08), 1200, 6000) * env(0.08, 0.0003, 0.012) + np.sin(2 * np.pi * 1700 * t(0.08)) * env(0.08, 0.0003, 0.015))

    # Clap: three quick bursts then a tail
    clap = np.zeros(len(t(0.45)))
    for k, offset in enumerate([0.0, 0.011, 0.022, 0.034]):
        start = int(offset * SR)
        burst = bandpass(noise(0.45 - offset), 900, 7000) * env(0.45 - offset, 0.0005, 0.012 if k < 3 else 0.12)
        clap[start:start + len(burst)] += burst
    write("Clap", clap)

    # Hats and cymbals
    write("Hat - closed", onepole_hp(metallic(0.09) * 0.5 + noise(0.09) * 0.5, 7000) * env(0.09, 0.0003, 0.02))
    write("Hat - open", onepole_hp(metallic(0.5) * 0.5 + noise(0.5) * 0.5, 6500) * env(0.5, 0.0005, 0.16))
    write("Shaker", bandpass(noise(0.14), 4000, 12000) * env(0.14, 0.02, 0.04))
    write("Ride", onepole_hp(metallic(1.6) * 0.7 + noise(1.6) * 0.15, 4000) * env(1.6, 0.0005, 0.6))
    write("Crash", onepole_hp(noise(2.2) * 0.7 + metallic(2.2) * 0.3, 3000) * env(2.2, 0.001, 0.75))

    # Toms
    for name, f0, f1 in [("Tom - low", 160, 80), ("Tom - mid", 220, 120), ("Tom - high", 300, 170)]:
        write(name, sweep(f0, f1, 0.6, 0.08) * env(0.6, 0.0008, 0.2) + bandpass(noise(0.6), 300, 3000) * env(0.6, 0.0005, 0.02) * 0.2)

    # Percussion and FX
    write("Perc - cowbell", saturate((np.sign(np.sin(2 * np.pi * 540 * t(0.35))) + np.sign(np.sin(2 * np.pi * 800 * t(0.35)))) * 0.3, 1.2) * env(0.35, 0.0005, 0.08))
    write("FX - reverse cymbal", (onepole_hp(noise(1.5), 3000) * env(1.5, 0.001, 0.6))[::-1])
    write("FX - sub drop", sweep(70, 30, 2.0, 0.6) * env(2.0, 0.005, 0.9))
    print("wrote", len(os.listdir(OUT)), "files to", OUT)


if __name__ == "__main__":
    main()
