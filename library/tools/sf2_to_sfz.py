"""Convert GeneralUser GS (bank 0 programs 0-127 + bank 128 preset 0) into SFZ
instruments that keep the SF2's original samples, loops and velocity layers.

Writes library/sfz/GeneralUser-GS/<NNN GM name>.sfz, "Drums - Standard Kit.sfz"
and samples/<safe sample name>_<shdr index>.wav (mono 16-bit, original rate).
Only opcodes the ClaudeDAW SFZ engine reads are emitted (see
docs/SF2_EXPORT_NOTES.md). Usage: python sf2_to_sfz.py [prog ...] [drums]
"""
import os
import re
import sys
from collections import Counter

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sf2_extract as x  # noqa: E402

LIB = x.LIB
OUT = os.path.join(LIB, "sfz", "GeneralUser-GS")
SMP = os.path.join(OUT, "samples")
DRUM_FILE = "Drums - Standard Kit.sfz"

# generator ids (in addition to sf2_extract's)
G_FC, G_Q, G_KN_HOLD, G_KN_DEC, G_KEYNUM, G_VELOCITY, G_EXCL = 8, 9, 39, 40, 46, 47, 57
G = x  # shorthand for the shared ids

ADDITIVE = (G.G_PAN, G.G_ATTEN, G.G_COARSE, G.G_FINE, G.G_SCALE, G.G_DELAY, G.G_ATT,
            G.G_HOLD, G.G_DEC, G.G_SUS, G.G_REL, G_FC, G_Q)
DEFAULTS = {G.G_PAN: 0, G.G_ATTEN: 0, G.G_COARSE: 0, G.G_FINE: 0, G.G_SCALE: 100,
            G.G_DELAY: -12000, G.G_ATT: -12000, G.G_HOLD: -12000, G.G_DEC: -12000,
            G.G_SUS: 0, G.G_REL: -12000, G_FC: 13500, G_Q: 0}

warnings = Counter()
written_samples = {}  # shdr index -> (filename, frames)


def wname(i, sh):
    n = re.sub(r"[^A-Za-z0-9._-]+", "_", x.cstr(sh[0])).strip("_.") or "sample"
    return "%s_%d.wav" % (n, i)


def write_sample(bank, si):
    if si in written_samples:
        return written_samples[si]
    sh = bank.shdr[si]
    _, start, end, ls, le, rate, opitch, pcorr, link, stype = sh
    if stype & 0x8000:
        written_samples[si] = None
        return None
    data = np.round(bank.smpl[start:end] * 32768.0).astype(np.int16)
    if len(data) < 2:
        written_samples[si] = None
        return None
    fn = wname(si, sh)
    sf.write(os.path.join(SMP, fn), data, rate, subtype="PCM_16")
    written_samples[si] = (fn, len(data))
    return written_samples[si]


def merged_zones(bank, pi):
    """Yield (gens, keyrange, velrange) per overlapping preset x instrument zone."""
    p, pn = bank.phdr[pi], bank.phdr[pi + 1]
    pglob, pzs = x.zones(bank.pbag, bank.pgen, p[3], pn[3], G.G_INST)
    for pz in pzs:
        pm = dict(pglob); pm.update(pz)
        ii = pz[G.G_INST]
        ins, insn = bank.inst[ii], bank.inst[ii + 1]
        iglob, izs = x.zones(bank.ibag, bank.igen, ins[1], insn[1], G.G_SAMPLE)
        pk, pv = x.rng(pm.get(G.G_KEYR)), x.rng(pm.get(G.G_VELR))
        for iz in izs:
            g = dict(iglob); g.update(iz)
            ik, iv = x.rng(g.get(G.G_KEYR)), x.rng(g.get(G.G_VELR))
            k = (max(pk[0], ik[0]), min(pk[1], ik[1]))
            v = (max(pv[0], iv[0]), min(pv[1], iv[1]))
            if k[0] > k[1] or v[0] > v[1]:
                continue
            for gid in ADDITIVE:
                if gid in pm:
                    g[gid] = g.get(gid, DEFAULTS[gid]) + pm[gid]
            yield g, k, v, x.cstr(ins[0])


def tc_sec(v):
    return 0.0 if v <= -12000 else 2.0 ** (v / 1200.0)


def fmt(v):
    s = ("%.4f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def region(bank, g, k, v, drums, label):
    si = g[G.G_SAMPLE]
    sh = bank.shdr[si]
    _, start, end, ls, le, rate, opitch, pcorr, link, stype = sh
    w = write_sample(bank, si)
    if w is None:
        warnings["%s: ROM/empty sample skipped" % label] += 1
        return None
    fn, n = w
    op = [("sample", fn)]
    op += [("lokey", k[0]), ("hikey", k[1])]  # never key=: it would also set pitch_keycenter
    if v != (0, 127):
        op += [("lovel", max(v[0], 1)), ("hivel", v[1])]
    # pitch
    root = g.get(G.G_ROOT, -1)
    if root is None or root < 0 or root > 127:
        root = opitch if opitch <= 127 else 60
    if g.get(G_KEYNUM, -1) >= 0:
        warnings["keynum generator ignored"] += 1
    op.append(("pitch_keycenter", root))
    cents = g.get(G.G_COARSE, 0) * 100 + g.get(G.G_FINE, 0) + pcorr
    tr = int(round(cents / 100.0))
    tune = cents - tr * 100
    if tr:
        op.append(("transpose", tr))
    if tune:
        op.append(("tune", tune))
    scale = g.get(G.G_SCALE, 100)
    if scale != 100:
        op.append(("pitch_keytrack", scale))
    # level / pan
    atten = min(max(g.get(G.G_ATTEN, 0), 0), 1440)
    if atten:
        op.append(("volume", fmt(-atten * 0.04)))
    pan = g.get(G.G_PAN, 0) / 5.0
    if stype & 4:
        pan -= 100
    elif stype & 2:
        pan += 100
    pan = max(-100.0, min(100.0, pan))
    if pan:
        op.append(("pan", fmt(pan)))
    # envelope
    for gid, name in ((G.G_DELAY, "ampeg_delay"), (G.G_ATT, "ampeg_attack"), (G.G_HOLD, "ampeg_hold"),
                      (G.G_DEC, "ampeg_decay"), (G.G_REL, "ampeg_release")):
        tc = g.get(gid, -12000)
        # keynumToVolEnvHold/Decay: evaluated once at the region's centre key
        kn = {G.G_HOLD: G_KN_HOLD, G.G_DEC: G_KN_DEC}.get(gid)
        if kn in g and tc > -12000:
            tc += (60 - (k[0] + k[1]) // 2) * g[kn]
        s = tc_sec(min(tc, 8000 if gid != G.G_DELAY else 5000))
        if s >= 0.0005:
            op.append((name, fmt(s)))
    sus = min(max(g.get(G.G_SUS, 0), 0), 1440)
    if sus:
        op.append(("ampeg_sustain", fmt(max(0.0, min(100.0, 100.0 * 10 ** (-sus / 200.0))))))
    if (G_KN_HOLD in g or G_KN_DEC in g) and k[1] - k[0] > 12:
        warnings["keynumToVolEnv* approximated over a range > 1 octave"] += 1
    # addresses
    s_off = g.get(G.G_START, 0) + 32768 * g.get(G.G_START_C, 0)
    e_off = g.get(G.G_END, 0) + 32768 * g.get(G.G_END_C, 0)
    ls_off = g.get(G.G_LS, 0) + 32768 * g.get(G.G_LS_C, 0)
    le_off = g.get(G.G_LE, 0) + 32768 * g.get(G.G_LE_C, 0)
    if s_off > 0:
        if s_off >= n - 1:
            warnings["%s: start offset beyond sample, ignored" % label] += 1
        else:
            op.append(("offset", s_off))
    elif s_off < 0:
        warnings["%s: negative start offset ignored" % label] += 1
    if e_off:
        e = n + e_off - 1
        if 0 < e < n - 1:
            op.append(("end", e))
        elif e >= n:
            warnings["%s: end offset past sample end, clamped" % label] += 1
    mode = g.get(G.G_MODES, 0) & 3
    if mode in (1, 3):
        lsr = ls - start + ls_off
        ler = le - start + le_off - 1
        if 0 <= lsr < ler < n:
            op += [("loop_mode", "loop_continuous" if mode == 1 else "loop_sustain"),
                   ("loop_start", lsr), ("loop_end", ler)]
        else:
            warnings["%s: invalid loop -> no_loop" % label] += 1
            op.append(("loop_mode", "one_shot" if drums else "no_loop"))
    else:
        op.append(("loop_mode", "one_shot" if drums else "no_loop"))
    # exclusive class
    ex = g.get(G_EXCL, 0)
    if ex > 0:
        op += [("group", ex), ("off_by", ex)]
    # filter
    fc = min(max(g.get(G_FC, 13500), 1500), 13500)
    if fc < 13500:
        q = min(max(g.get(G_Q, 0), 0), 960)
        op += [("fil_type", "lpf_2p"), ("cutoff", fmt(8.176 * 2 ** (fc / 1200.0)))]
        if q:
            op.append(("resonance", fmt(q / 10.0)))
    return op


def convert(bank, bankno, prog, label, drums=False):
    pi = bank.preset_index(bankno, prog)
    if pi is None:
        warnings["missing preset %d:%d" % (bankno, prog)] += 1
        return None
    pname = x.cstr(bank.phdr[pi][0])
    lines = ["// %s - GeneralUser GS 2.0.3 preset \"%s\" (bank %d prog %d), converted by sf2_to_sfz.py"
             % (label, pname, bankno, prog), "<control> default_path=samples/", ""]
    nreg = 0
    for g, k, v, iname in merged_zones(bank, pi):
        op = region(bank, g, k, v, drums, label)
        if op is None:
            continue
        lines.append("<region> " + " ".join("%s=%s" % (a, b) for a, b in op))
        nreg += 1
    fn = DRUM_FILE if drums else x.safe(label) + ".sfz"
    with open(os.path.join(OUT, fn), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return fn, pname, nreg


def main():
    bank = x.Bank(x.SF2)
    os.makedirs(SMP, exist_ok=True)
    args = sys.argv[1:]
    progs = range(128) if not args else [int(a) for a in args if a.isdigit()]
    do_drums = not args or "drums" in args
    total = 0
    count = 0
    for prog in progs:
        label = "%03d %s" % (prog, x.GM_NAMES[prog])
        r = convert(bank, 0, prog, label)
        if r:
            count += 1; total += r[2]
            print("%s | %s | %d regions" % (label, r[1], r[2]), flush=True)
    if do_drums:
        r = convert(bank, 128, 0, "Drums - Standard Kit", drums=True)
        if r:
            count += 1; total += r[2]
            print("drums | %s | %d regions" % (r[1], r[2]))
    ws = [w for w in written_samples.values() if w]
    mb = sum(os.path.getsize(os.path.join(SMP, w[0])) for w in ws) / 1e6
    print("presets written %d, regions %d, samples %d, %.1f MB" % (count, total, len(ws), mb))
    for w, c in sorted(warnings.items()):
        print("WARNING %s (x%d)" % (w, c))


if __name__ == "__main__":
    main()
