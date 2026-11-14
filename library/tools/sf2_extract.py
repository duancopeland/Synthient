"""Extract GeneralUser GS (bank 0 programs 0-127 + bank 128 preset 0) into WAV
regions for the Tracktion SamplerPlugin.

For each preset, the zones that sound at velocity 100 are found per key. Keys
with the same set of zones form a segment; each segment is rendered once (all
its zones mixed, including stereo L/R pairs and layers) at a root note inside
the segment. Looped samples are rendered to ~3 s by repeating the loop, with a
simple SF2 volume envelope (delay/attack/hold/decay/sustain) and a release
fade, because Tracktion's sampler does not loop. Not modelled: filters,
modulators, LFOs, chorus/reverb sends.

Writes sf2/GeneralUser-GS-extracted/<NNN Name>/k<root>.wav and
sf2/GeneralUser-GS-extracted/regions.json (consumed by build_index.py).
"""
import json
import math
import os
import re
import struct
import sys

import numpy as np
import soundfile as sf

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SF2 = os.path.join(LIB, "sf2", "GeneralUser-GS.sf2")
OUT = os.path.join(LIB, "sf2", "GeneralUser-GS-extracted")
SR = 44100
VEL = 100
MAX_REGIONS = 64

GM_NAMES = """Acoustic Grand Piano|Bright Acoustic Piano|Electric Grand Piano|Honky-tonk Piano|Electric Piano 1|Electric Piano 2|Harpsichord|Clavinet|Celesta|Glockenspiel|Music Box|Vibraphone|Marimba|Xylophone|Tubular Bells|Dulcimer|Drawbar Organ|Percussive Organ|Rock Organ|Church Organ|Reed Organ|Accordion|Harmonica|Tango Accordion|Acoustic Guitar (nylon)|Acoustic Guitar (steel)|Electric Guitar (jazz)|Electric Guitar (clean)|Electric Guitar (muted)|Overdriven Guitar|Distortion Guitar|Guitar Harmonics|Acoustic Bass|Electric Bass (finger)|Electric Bass (pick)|Fretless Bass|Slap Bass 1|Slap Bass 2|Synth Bass 1|Synth Bass 2|Violin|Viola|Cello|Contrabass|Tremolo Strings|Pizzicato Strings|Orchestral Harp|Timpani|String Ensemble 1|String Ensemble 2|Synth Strings 1|Synth Strings 2|Choir Aahs|Voice Oohs|Synth Voice|Orchestra Hit|Trumpet|Trombone|Tuba|Muted Trumpet|French Horn|Brass Section|Synth Brass 1|Synth Brass 2|Soprano Sax|Alto Sax|Tenor Sax|Baritone Sax|Oboe|English Horn|Bassoon|Clarinet|Piccolo|Flute|Recorder|Pan Flute|Blown Bottle|Shakuhachi|Whistle|Ocarina|Lead 1 (square)|Lead 2 (sawtooth)|Lead 3 (calliope)|Lead 4 (chiff)|Lead 5 (charang)|Lead 6 (voice)|Lead 7 (fifths)|Lead 8 (bass + lead)|Pad 1 (new age)|Pad 2 (warm)|Pad 3 (polysynth)|Pad 4 (choir)|Pad 5 (bowed)|Pad 6 (metallic)|Pad 7 (halo)|Pad 8 (sweep)|FX 1 (rain)|FX 2 (soundtrack)|FX 3 (crystal)|FX 4 (atmosphere)|FX 5 (brightness)|FX 6 (goblins)|FX 7 (echoes)|FX 8 (sci-fi)|Sitar|Banjo|Shamisen|Koto|Kalimba|Bagpipe|Fiddle|Shanai|Tinkle Bell|Agogo|Steel Drums|Woodblock|Taiko Drum|Melodic Tom|Synth Drum|Reverse Cymbal|Guitar Fret Noise|Breath Noise|Seashore|Bird Tweet|Telephone Ring|Helicopter|Applause|Gunshot""".split("|")
assert len(GM_NAMES) == 128

# generator ids
G_START, G_END, G_LS, G_LE, G_START_C = 0, 1, 2, 3, 4
G_END_C, G_PAN, G_DELAY, G_ATT, G_HOLD, G_DEC, G_SUS, G_REL = 12, 17, 33, 34, 35, 36, 37, 38
G_INST, G_KEYR, G_VELR, G_LS_C, G_ATTEN, G_LE_C = 41, 43, 44, 45, 48, 50
G_COARSE, G_FINE, G_SAMPLE, G_MODES, G_SCALE, G_ROOT = 51, 52, 53, 54, 56, 58
ENV_DEFAULT = {G_DELAY: -12000, G_ATT: -12000, G_HOLD: -12000, G_DEC: -12000, G_SUS: 0, G_REL: -12000}


def read_sf2(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"RIFF" and data[8:12] == b"sfbk"
    chunks = {}

    def walk(off, end):
        while off + 8 <= end:
            cid = data[off:off + 4]
            size = struct.unpack_from("<I", data, off + 4)[0]
            if cid == b"LIST":
                walk(off + 12, off + 8 + size)
            else:
                chunks[cid.decode("latin-1")] = (off + 8, size)
            off += 8 + size + (size & 1)

    walk(12, len(data))

    def recs(name, fmt):
        o, s = chunks[name]
        n = struct.calcsize(fmt)
        return [struct.unpack_from(fmt, data, o + i * n) for i in range(s // n)]

    o, s = chunks["smpl"]
    smpl = np.frombuffer(data, dtype="<i2", count=s // 2, offset=o).astype(np.float32) / 32768.0
    phdr = recs("phdr", "<20sHHHIII")
    pbag = recs("pbag", "<HH")
    pgen = recs("pgen", "<Hh")
    inst = recs("inst", "<20sH")
    ibag = recs("ibag", "<HH")
    igen = recs("igen", "<Hh")
    shdr = recs("shdr", "<20sIIIIIBbHH")
    return smpl, phdr, pbag, pgen, inst, ibag, igen, shdr


def cstr(b):
    return b.split(b"\0", 1)[0].decode("latin-1").strip()


def zones(bags, gens, first, last, term_gen):
    """Return (global_gens, [zone_gens]) for bag indices [first, last)."""
    out, glob = [], {}
    for bi in range(first, last):
        g0, g1 = bags[bi][0], bags[bi + 1][0]
        z = {}
        for oper, amt in gens[g0:g1]:
            z[oper] = amt
        if term_gen in z:
            out.append(z)
        elif bi == first:
            glob = z
    return glob, out


def rng(amt, default=(0, 127)):
    if amt is None:
        return default
    u = amt & 0xFFFF
    return (u & 0xFF, (u >> 8) & 0xFF)


def ucoarse(v):
    return v & 0xFFFF if v is not None else 0


class Bank:
    def __init__(self, path):
        (self.smpl, self.phdr, self.pbag, self.pgen, self.inst, self.ibag,
         self.igen, self.shdr) = read_sf2(path)

    def preset_index(self, bank, prog):
        for i, p in enumerate(self.phdr[:-1]):
            if p[2] == bank and p[1] == prog:
                return i
        return None

    def preset_zones(self, pi):
        """Flatten preset -> list of fully merged playable zones."""
        p, pn = self.phdr[pi], self.phdr[pi + 1]
        pglob, pzs = zones(self.pbag, self.pgen, p[3], pn[3], G_INST)
        result = []
        for pz in pzs:
            pmerged = dict(pglob); pmerged.update(pz)
            ii = pz[G_INST]
            ins, insn = self.inst[ii], self.inst[ii + 1]
            iglob, izs = zones(self.ibag, self.igen, ins[1], insn[1], G_SAMPLE)
            pk = rng(pmerged.get(G_KEYR)); pv = rng(pmerged.get(G_VELR))
            for iz in izs:
                g = dict(iglob); g.update(iz)
                ik = rng(g.get(G_KEYR)); iv = rng(g.get(G_VELR))
                k = (max(pk[0], ik[0]), min(pk[1], ik[1]))
                v = (max(pv[0], iv[0]), min(pv[1], iv[1]))
                if k[0] > k[1] or v[0] > v[1]:
                    continue
                # additive preset-level generators
                for gid in (G_PAN, G_ATTEN, G_COARSE, G_FINE, G_DELAY, G_ATT, G_HOLD, G_DEC, G_SUS, G_REL):
                    if gid in pmerged and gid not in (G_KEYR, G_VELR):
                        base = g.get(gid, ENV_DEFAULT.get(gid, 0))
                        g[gid] = base + pmerged[gid]
                g["keys"], g["vels"] = k, v
                g["inst_name"] = cstr(ins[0])
                result.append(g)
        return result

    def render_zone(self, z, note, seconds_loop=3.0, cap=8.0):
        sh = self.shdr[z[G_SAMPLE]]
        _, start, end, ls, le, rate, opitch, pcorr, link, stype = sh
        start += z.get(G_START, 0) + 32768 * z.get(G_START_C, 0)
        end += z.get(G_END, 0) + 32768 * z.get(G_END_C, 0)
        ls += z.get(G_LS, 0) + 32768 * z.get(G_LS_C, 0)
        le += z.get(G_LE, 0) + 32768 * z.get(G_LE_C, 0)
        src = self.smpl[start:end]
        if len(src) < 2:
            return None, False
        ls -= start; le -= start
        root = z.get(G_ROOT, -1)
        if root is None or root < 0:
            root = opitch if opitch <= 127 else 60
        scale = z.get(G_SCALE, 100)
        cents = (note - root) * scale + z.get(G_COARSE, 0) * 100 + z.get(G_FINE, 0) + pcorr
        ratio = (2.0 ** (cents / 1200.0)) * rate / SR
        mode = z.get(G_MODES, 0) & 3
        looped = mode in (1, 3) and 0 <= ls < le <= len(src) and (le - ls) > 8
        # envelope (timecents -> seconds)
        tc = lambda gid: 2.0 ** (z.get(gid, ENV_DEFAULT[gid]) / 1200.0)
        delay, att, hold, dec = tc(G_DELAY), tc(G_ATT), tc(G_HOLD), tc(G_DEC)
        sus_db = -min(max(z.get(G_SUS, 0), 0), 1440) / 10.0
        rel = min(max(tc(G_REL), 0.05), 0.6)
        if looped:
            # sustain-level sound: ~3 s; decaying sound: until env reaches -80 dB
            if sus_db > -60:
                dur = max(seconds_loop, (ls / ratio) / SR + 0.3)
            else:
                dur = delay + att + hold + dec * 0.8
            dur = min(max(dur, 0.3), cap)
        else:
            dur = min((len(src) - 1) / ratio / SR, cap)
        n = int(dur * SR)
        if n < 16:
            return None, False
        pos = np.arange(n, dtype=np.float64) * ratio
        if looped:
            over = pos >= le
            pos[over] = ls + np.mod(pos[over] - ls, le - ls)
        pos = np.clip(pos, 0, len(src) - 1)
        out = np.interp(pos, np.arange(len(src)), src).astype(np.float32)
        # envelope
        t = np.arange(n) / SR
        env_db = np.zeros(n, dtype=np.float32)
        t1 = delay + att + hold
        if dec > 0.002:
            env_db = np.where(t > t1, np.maximum(-100.0 * (t - t1) / dec, sus_db), 0.0)
        elif sus_db < 0:
            env_db = np.where(t > t1, sus_db, 0.0)
        gain = 10.0 ** (env_db / 20.0)
        if att > 0.002:
            gain *= np.clip((t - delay) / att, 0, 1)
        if delay > 0.002:
            gain *= (t >= delay)
        out *= gain.astype(np.float32)
        # release fade at the end (looped) / tiny anti-click fade (one-shot)
        fl = int((rel if looped else 0.01) * SR)
        fl = min(fl, n // 2)
        if fl > 0:
            out[-fl:] *= np.linspace(1, 0, fl, dtype=np.float32) ** 2
        out *= 10.0 ** (-z.get(G_ATTEN, 0) / 10.0 / 20.0 * 0.4)  # SF2 atten is 0.4 dB/cB in practice
        # pan: explicit pan gen, or L/R sample type
        pan = z.get(G_PAN, 0) / 500.0
        if stype & 4:      # left sample
            pan = -1.0 if G_PAN not in z else pan
        elif stype & 2:    # right sample
            pan = 1.0 if G_PAN not in z else pan
        pan = max(-1.0, min(1.0, pan))
        a = (pan + 1) * math.pi / 4
        st = np.stack([out * math.cos(a), out * math.sin(a)], axis=1) * math.sqrt(2)
        return st, looped


def active_zones(zs, key):
    cand = [i for i, z in enumerate(zs) if z["keys"][0] <= key <= z["keys"][1]]
    if not cand:
        return ()
    on = [i for i in cand if zs[i]["vels"][0] <= VEL <= zs[i]["vels"][1]]
    if not on:
        def dist(i):
            lo, hi = zs[i]["vels"]
            return VEL - hi if hi < VEL else lo - VEL
        d = min(dist(i) for i in cand)
        on = [i for i in cand if dist(i) == d]
    return tuple(on)


def mix(parts):
    parts = [p for p in parts if p is not None]
    if not parts:
        return None
    n = max(len(p) for p in parts)
    out = np.zeros((n, 2), dtype=np.float32)
    for p in parts:
        out[:len(p)] += p
    return out


def safe(name):
    return re.sub(r'[<>:"/\\|?*]+', "-", name).strip(" .")


def extract(bank, bankno, prog, label, drums=False):
    pi = bank.preset_index(bankno, prog)
    if pi is None:
        return None
    pname = cstr(bank.phdr[pi][0])
    zs = bank.preset_zones(pi)
    keymap = {k: active_zones(zs, k) for k in range(128)}
    keymap = {k: v for k, v in keymap.items() if v}
    folder = os.path.join(OUT, safe(label))
    os.makedirs(folder, exist_ok=True)
    renders = []  # (lo, hi, root, audio)
    if drums:
        keys = sorted(keymap)
        if len(keys) > MAX_REGIONS:
            gm = [k for k in keys if 35 <= k <= 81]
            rest = [k for k in keys if k not in gm]
            keys = sorted(gm + rest[:MAX_REGIONS - len(gm)])
        for k in keys:
            renders.append((k, k, k, mix([bank.render_zone(zs[i], k)[0] for i in keymap[k]])))
    else:
        segs = []
        for k in sorted(keymap):
            if segs and segs[-1][2] == keymap[k] and segs[-1][1] == k - 1:
                segs[-1][1] = k
            else:
                segs.append([k, k, keymap[k]])
        for s in segs:
            roots = [zs[i].get(G_ROOT, -1) for i in s[2]]
            roots = [r if r is not None and r >= 0 else bank.shdr[zs[i][G_SAMPLE]][6] for r, i in zip(roots, s[2])]
            s.append(min(max(roots[0], s[0]), s[1]))
        if len(segs) > MAX_REGIONS:
            step = -(-len(segs) // MAX_REGIONS)
            kept = segs[::step]
            while len(kept) > MAX_REGIONS:
                step += 1
                kept = segs[::step]
            assign = {}
            for k in keymap:
                best = min(range(len(kept)), key=lambda j: (abs(kept[j][3] - k), kept[j][3]))
                assign.setdefault(best, []).append(k)
            newsegs = []
            for j, ks in assign.items():
                ks.sort()
                # split non-contiguous runs (gaps in key coverage)
                run = [ks[0]]
                for k in ks[1:]:
                    if k == run[-1] + 1:
                        run.append(k)
                    else:
                        newsegs.append([run[0], run[-1], kept[j][2], kept[j][3]]); run = [k]
                newsegs.append([run[0], run[-1], kept[j][2], kept[j][3]])
            segs = sorted(newsegs)
        cache = {}
        for lo, hi, act, root in segs:
            key = (act, root)
            if key not in cache:
                cache[key] = mix([bank.render_zone(zs[i], root)[0] for i in act])
            renders.append((lo, hi, root, cache[key]))
    peak = max((float(np.max(np.abs(a))) for *_, a in renders if a is not None), default=0)
    if peak <= 0:
        return None
    g = 10 ** (-1.0 / 20) / peak
    regions, written = [], {}
    for lo, hi, root, a in renders:
        if a is None:
            continue
        fn = "k%03d.wav" % root
        if id(a) not in written:
            if fn in written.values():
                fn = "k%03d_%d-%d.wav" % (root, lo, hi)
            sf.write(os.path.join(folder, fn), np.clip(a * g, -1, 1), SR, subtype="PCM_16")
            written[id(a)] = fn
        fn = written[id(a)]
        regions.append({"path": os.path.relpath(os.path.join(folder, fn), LIB).replace("\\", "/"),
                        "lo": lo, "hi": hi, "root": root})
    return {"sf2_name": pname, "regions": regions}


def main():
    bank = Bank(SF2)
    os.makedirs(OUT, exist_ok=True)
    out = []
    progs = range(128) if len(sys.argv) < 2 else [int(x) for x in sys.argv[1:]]
    for prog in progs:
        label = "%03d %s" % (prog, GM_NAMES[prog])
        r = extract(bank, 0, prog, label)
        if r is None:
            print("missing program", prog); continue
        r.update({"gm_program": prog, "gm_name": GM_NAMES[prog]})
        out.append(r)
        print(label, "|", r["sf2_name"], "|", len(r["regions"]), "regions", flush=True)
    if len(sys.argv) < 2:
        r = extract(bank, 128, 0, "Drums - Standard Kit", drums=True)
        if r:
            r.update({"gm_program": None, "gm_name": "Standard Drum Kit", "drums": True})
            out.append(r)
            print("drums |", r["sf2_name"], "|", len(r["regions"]), "regions")
        with open(os.path.join(OUT, "regions.json"), "w", encoding="utf-8") as f:
            json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
