"""Minimal SFZ reader + "one velocity layer, one sample per key" reducer for the
Tracktion SamplerPlugin (<=64 sounds, root/min/max key, no layers, no RR).

Handles: comments, #define/#include, <control> default_path + set_ccN,
<global>/<master>/<group>/<region> inheritance, key/lokey/hikey/pitch_keycenter
as numbers or note names, lovel/hivel, trigger (release/legato skipped),
seq_position / lorand round robins (first kept), loccN/hiccN against set_cc
defaults, sw_last keyswitch variants.
"""
import os
import re

NOTE_RE = re.compile(r"^([a-gA-G])([#b]?)(-?\d+)$")
NOTE_BASE = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


def note_num(v):
    if v is None:
        return None
    v = str(v).strip()
    try:
        return int(float(v))
    except ValueError:
        pass
    m = NOTE_RE.match(v)
    if not m:
        return None
    n = NOTE_BASE[m.group(1).lower()] + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return n + (int(m.group(3)) + 1) * 12  # c4 = 60


def _read(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("utf-8", "latin-1"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue


def preprocess(path, root_dir, defines=None, depth=0):
    """Return text with comments stripped, #include expanded, #define applied."""
    defines = {} if defines is None else defines
    text = _read(path)
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    out = []
    for line in text.splitlines():
        line = line.split("//", 1)[0]
        s = line.strip()
        m = re.match(r'#define\s+(\$\w+)\s+(.*)$', s)
        if m:
            defines[m.group(1)] = m.group(2).strip()
            continue
        m = re.match(r'#include\s+"([^"]+)"', s)
        if m:
            inc = os.path.join(root_dir, m.group(1).replace("\\", "/"))
            if depth < 8 and os.path.isfile(inc):
                out.append(preprocess(inc, root_dir, defines, depth + 1))
            continue
        for k in sorted(defines, key=len, reverse=True):
            if k in line:
                line = line.replace(k, defines[k])
        out.append(line)
    return "\n".join(out)


TOKEN_RE = re.compile(r"<(\w+)>|([A-Za-z0-9_]+)=")


def parse(path):
    """Return (control, regions) where regions are merged opcode dicts in file order."""
    root_dir = os.path.dirname(path)
    text = preprocess(path, root_dir)
    control, glob, master, group = {}, {}, {}, {}
    regions = []
    cur = None
    cur_name = None
    toks = list(TOKEN_RE.finditer(text))

    def close():
        if cur_name == "region" and cur is not None:
            r = {}
            r.update(glob); r.update(master); r.update(group); r.update(cur)
            regions.append(r)

    for i, m in enumerate(toks):
        if m.group(1):
            close()
            h = m.group(1).lower()
            cur_name = h
            if h == "control":
                cur = control
            elif h == "global":
                glob = {}; master = {}; group = {}; cur = glob
            elif h == "master":
                master = {}; group = {}; cur = master
            elif h == "group":
                group = {}; cur = group
            elif h == "region":
                cur = {}
            else:
                cur = {}  # curve/effect/etc: ignored
        else:
            end = toks[i + 1].start() if i + 1 < len(toks) else len(text)
            val = text[m.end():end].strip()
            if cur is not None:
                cur[m.group(2).lower()] = val
    close()
    return control, regions


def region_keys(r):
    key = note_num(r.get("key"))
    lo = note_num(r.get("lokey"))
    hi = note_num(r.get("hikey"))
    if key is not None:
        lo = key if lo is None else lo
        hi = key if hi is None else hi
    lo = 0 if lo is None else lo
    hi = 127 if hi is None else hi
    pk = r.get("pitch_keycenter")
    root = note_num(pk) if pk and pk.lower() != "sample" else None
    if root is None:
        root = key if key is not None else (lo if lo == hi else 60)
    tr = r.get("transpose")
    if tr:
        try:
            root -= int(float(tr))
        except ValueError:
            pass
    return lo, hi, root


def cc_defaults(control):
    d = {}
    for k, v in control.items():
        m = re.match(r"set_cc(\d+)$", k)
        if m:
            try:
                d[int(m.group(1))] = float(v)
            except ValueError:
                pass
    return d


def keep_region(r, ccs, sw=None):
    trig = r.get("trigger", "attack").lower()
    if trig not in ("attack", "first"):
        return False
    try:
        if float(r.get("lorand", 0)) > 0.0:
            return False
    except ValueError:
        pass
    try:
        if int(float(r.get("seq_position", 1))) != 1:
            return False
    except ValueError:
        pass
    for k, v in r.items():
        m = re.match(r"(lo|hi)cc(\d+)$", k)
        if m:
            c = ccs.get(int(m.group(2)), 0.0)
            try:
                lim = float(v)
            except ValueError:
                continue
            if m.group(1) == "lo" and c < lim:
                return False
            if m.group(1) == "hi" and c > lim:
                return False
    if sw is not None and "sw_last" in r:
        if note_num(r["sw_last"]) != sw:
            return False
    if "sample" not in r or r["sample"].startswith("*"):
        return False
    return True


def vel_score(r, target=100):
    lo = int(float(r.get("lovel", 1)))
    hi = int(float(r.get("hivel", 127)))
    if lo <= target <= hi:
        return 0
    return (target - hi) if hi < target else (lo - target) + 0.5


def sample_path(r, control, sfz_dir):
    p = control.get("default_path", "") + r["sample"]
    p = p.replace("\\", "/").strip()
    return os.path.normpath(os.path.join(sfz_dir, p))


def coalesce(keymap):
    """keymap: {key: (path, root)} -> list of regions with contiguous ranges."""
    regs = []
    for k in sorted(keymap):
        p, root = keymap[k]
        if regs and regs[-1]["path"] == p and regs[-1]["root"] == root and regs[-1]["hi"] == k - 1:
            regs[-1]["hi"] = k
        else:
            regs.append({"path": p, "lo": k, "hi": k, "root": root})
    return regs


def thin(regs, limit=64):
    if len(regs) <= limit:
        return regs
    # keep every Nth distinct sample, reassign every covered key to nearest kept root
    step = -(-len(regs) // limit)
    while True:
        kept = regs[::step]
        if len(kept) <= limit:
            break
        step += 1
    keymap = {}
    for r in regs:
        for k in range(r["lo"], r["hi"] + 1):
            best = min(kept, key=lambda q: (abs(q["root"] - k), q["root"]))
            keymap[k] = (best["path"], best["root"])
    return coalesce(keymap)


def reduce_instrument(path, sw=None, exclude=None, velocity=100, drums=False):
    control, regions = parse(path)
    ccs = cc_defaults(control)
    sfz_dir = os.path.dirname(path)
    best = {}  # key -> (score, order, path, root)
    for order, r in enumerate(regions):
        if not keep_region(r, ccs, sw):
            continue
        sp = sample_path(r, control, sfz_dir)
        if exclude and re.search(exclude, sp.replace("\\", "/"), re.I):
            continue
        lo, hi, root = region_keys(r)
        sc = vel_score(r, velocity)
        for k in range(max(0, lo), min(127, hi) + 1):
            cur = best.get(k)
            if cur is None or (sc, order) < (cur[0], cur[1]):
                best[k] = (sc, order, sp, root)
    keymap = {k: (v[2], v[3]) for k, v in best.items()}
    regs = coalesce(keymap)
    if drums:
        regs = regs[:64]
    else:
        regs = thin(regs)
    return regs, control, regions
