"""Download Synthient's instrument library from each project's own release.

Usage (any Python 3.8+; the GeneralUser step also needs numpy and soundfile, which
Synthient's bundled analysis Python has):

    python library/tools/get_library.py             # everything marked default in sources.json
    python library/tools/get_library.py --list      # what there is, and what's installed
    python library/tools/get_library.py --only surge,vsco
    python library/tools/get_library.py --skip vsco

Works on the library folder this script lives in (library/tools/..). Downloads go to
library/.downloads and are deleted once a part is installed. A part that is already installed
(a marker in library/.installed) is skipped, so running it again resumes after a failure.
Exit code: 0 when every selected part is installed, 1 if any failed.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(TOOLS)
DOWNLOADS = os.path.join(LIB, ".downloads")
MARKERS = os.path.join(LIB, ".installed")
USER_AGENT = "Synthient-library-installer/1.0"


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.1f} {unit}" if unit in ("MB", "GB") else f"{n:.0f} {unit}"
        n /= 1024


def fsize(f):
    """A file's download size in bytes: exact when known, else the approximate 'mb'."""
    return f.get("size") or int(f.get("mb", 0) * 1024 * 1024)


def lib_path(rel):
    return os.path.join(LIB, rel.replace("/", os.sep))


# ---- downloading ---------------------------------------------------------------------------

def download(url, dest, size=None, sha256=None, tries=4):
    """Fetch url to dest, resuming a partial .part file when the server allows it."""
    part = dest + ".part"
    for attempt in range(1, tries + 1):
        try:
            have = os.path.getsize(part) if os.path.exists(part) else 0
            if size and have >= size:  # complete or oversized leftover: the server would refuse the range
                os.remove(part)
                have = 0
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            if have:
                req.add_header("Range", f"bytes={have}-")
            with urllib.request.urlopen(req, timeout=60) as r:
                if have and r.status != 206:  # no resume support: start over
                    have = 0
                total = size or (int(r.headers["Content-Length"]) + have if r.headers.get("Content-Length") else None)
                done, start, last = have, time.time(), 0.0
                with open(part, "ab" if have else "wb") as f:
                    while True:
                        chunk = r.read(1 << 20)
                        if not chunk:
                            break
                        f.write(chunk)
                        done += len(chunk)
                        now = time.time()
                        if now - last > 0.5:
                            last = now
                            rate = (done - have) / max(now - start, 0.001)
                            pct = f"{100 * done / total:3.0f}%  " if total else ""
                            of = f" of {human(total)}" if total else ""
                            sys.stdout.write(f"\r    {pct}{human(done)}{of}  {human(rate)}/s      ")
                            sys.stdout.flush()
            sys.stdout.write("\r" + " " * 70 + "\r")
            if size and os.path.getsize(part) != size:
                raise IOError(f"got {os.path.getsize(part)} bytes, expected {size}")
            if sha256:
                h = hashlib.sha256()
                with open(part, "rb") as f:
                    for chunk in iter(lambda: f.read(1 << 20), b""):
                        h.update(chunk)
                if h.hexdigest().lower() != sha256.lower():
                    os.remove(part)
                    raise IOError("checksum mismatch (the download is damaged or the file changed upstream)")
            os.replace(part, dest)
            return
        except Exception as e:  # noqa: BLE001 - report and retry any network/disk error
            sys.stdout.write("\n")
            if (getattr(e, "code", None) == 416 or isinstance(e, IOError) and "expected" in str(e)) and os.path.exists(part):
                os.remove(part)  # a bad partial file: start this download over
            if attempt == tries:
                raise
            print(f"    {e}; retrying ({attempt}/{tries - 1})...")
            time.sleep(3 * attempt)


# ---- extracting ----------------------------------------------------------------------------

class Archive:
    """Uniform read access to zip and tar(.xz/.gz) archives."""

    def __init__(self, path):
        self.path = path
        if zipfile.is_zipfile(path):
            self.zip = zipfile.ZipFile(path)
            self.names = [n for n in self.zip.namelist() if not n.endswith("/")]
        else:
            self.zip = None
            self.tar = tarfile.open(path)
            self.members = {m.name: m for m in self.tar.getmembers() if m.isfile()}
            self.names = list(self.members)

    def top(self):
        tops = {n.split("/", 1)[0] for n in self.names}
        if len(tops) != 1:
            raise ValueError(f"expected one top-level folder in {os.path.basename(self.path)}, found {len(tops)}")
        return tops.pop()

    def extract(self, name, dest):
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        src = self.zip.open(name) if self.zip else self.tar.extractfile(self.members[name])
        with src, open(dest, "wb") as out:
            shutil.copyfileobj(src, out, 1 << 20)

    def close(self):
        (self.zip or self.tar).close()


def place(archive, src, dst):
    """Extract archive path src (a folder or a file; a leading '*/' is the archive's single
    top-level folder) to library path dst, via dst.partial so a failure leaves no half folder."""
    if src == "*" or src.startswith("*/"):
        src = archive.top() + src[1:]
    src = src.rstrip("/")
    target = lib_path(dst)
    tmp = target + ".partial"
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    elif os.path.exists(tmp):
        os.remove(tmp)
    if src in archive.names:  # a single file
        archive.extract(src, tmp)
    else:
        prefix = src + "/"
        files = [n for n in archive.names if n.startswith(prefix)]
        if not files:
            raise FileNotFoundError(f"{src} not found in {os.path.basename(archive.path)}")
        for n in files:
            rel = n[len(prefix):]
            if os.path.isabs(rel) or ".." in rel.split("/"):
                raise ValueError(f"unsafe path in archive: {n}")
            archive.extract(n, os.path.join(tmp, rel.replace("/", os.sep)))
    if os.path.isdir(target):
        shutil.rmtree(target)
    elif os.path.exists(target):
        os.remove(target)
    os.replace(tmp, target)


# ---- parts -----------------------------------------------------------------------------------

def installed(part):
    """Every destination (and any extra 'check' path) exists. Destinations are only renamed into
    place once fully unpacked, so this also accepts a library that was copied in by hand or
    shipped in a full package, without downloading it again."""
    paths = [dst for f in part["files"] for dst in f["place"].values()] + part.get("check", [])
    return all(os.path.exists(lib_path(p)) for p in paths)


def install(part, keep):
    os.makedirs(DOWNLOADS, exist_ok=True)
    for f in part["files"]:
        dest = os.path.join(DOWNLOADS, f["name"])
        if not os.path.exists(dest):
            print(f"  Downloading {f['name']} ({'' if f.get('size') else 'about '}{human(fsize(f))})")
            download(f["url"], dest, f.get("size"), f.get("sha256"))
        print(f"  Unpacking {f['name']}")
        archive = Archive(dest)
        try:
            for src, dst in f["place"].items():
                place(archive, src, dst)
        finally:
            archive.close()
    for rel, text in part.get("write", {}).items():
        with open(lib_path(rel), "w", encoding="utf-8") as out:
            out.write(text + "\n")
    if part.get("post"):
        print(f"  Converting ({part['post']}); this takes a few minutes")
        r = subprocess.run([sys.executable, os.path.join(TOOLS, part["post"])], cwd=TOOLS,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"{part['post']} failed: {r.stderr.strip()[-500:]}")
    os.makedirs(MARKERS, exist_ok=True)
    with open(os.path.join(MARKERS, part["id"] + ".json"), "w", encoding="utf-8") as out:
        json.dump({"version": part.get("version"), "installed": time.strftime("%Y-%m-%d %H:%M")}, out)
    if not keep:
        for f in part["files"]:
            p = os.path.join(DOWNLOADS, f["name"])
            if os.path.exists(p):
                os.remove(p)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--list", action="store_true", help="show the parts and exit")
    ap.add_argument("--only", help="comma-separated part ids to install")
    ap.add_argument("--skip", help="comma-separated part ids to leave out")
    ap.add_argument("--all", action="store_true", help="include optional parts")
    ap.add_argument("--force", action="store_true", help="reinstall parts that are already installed")
    ap.add_argument("--keep-downloads", action="store_true", help="keep the downloaded archives")
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)

    with open(os.path.join(LIB, "sources.json"), encoding="utf-8") as f:
        parts = json.load(f)["parts"]
    ids = {p["id"] for p in parts}
    only = set(args.only.split(",")) if args.only else None
    skip = set(args.skip.split(",")) if args.skip else set()
    for bad in (only or set()) | skip:
        if bad not in ids:
            sys.exit(f"Unknown part '{bad}'. Parts: {', '.join(sorted(ids))}")

    if args.list:
        for p in parts:
            dl = sum(fsize(f) for f in p["files"])
            state = "installed" if installed(p) else ("default" if p.get("default", True) else "optional")
            print(f"{p['id']:<12} {p['title']:<44} {human(dl):>9} download, {p['disk_mb'] / 1024:.1f} GB on disk  [{state}]")
        return 0

    chosen = [p for p in parts if (p["id"] in only if only else (args.all or p.get("default", True))) and p["id"] not in skip]
    todo = [p for p in chosen if args.force or not installed(p)]
    for p in chosen:
        if p not in todo:
            print(f"{p['title']}: already installed")
    if not todo:
        print("Nothing to download.")
        return 0

    # Room for every part, plus the biggest single download while it is being unpacked.
    need = sum(p["disk_mb"] for p in todo) * 1024 * 1024 + max(sum(fsize(f) for f in p["files"]) for p in todo)
    free = shutil.disk_usage(LIB).free
    if free < need:
        print(f"Not enough free space in {LIB}: {human(free)} free, about {human(need)} needed.")
        return 1

    total = sum(fsize(f) for p in todo for f in p["files"])
    print(f"Installing {len(todo)} part(s) into {LIB} (about {human(total)} to download)\n")
    failed = []
    for i, p in enumerate(todo, 1):
        print(f"[{i}/{len(todo)}] {p['title']}")
        try:
            install(p, args.keep_downloads)
            print("  Done")
        except Exception as e:  # noqa: BLE001 - keep going with the other parts
            print(f"  FAILED: {e}")
            failed.append(p["title"])
    if os.path.isdir(DOWNLOADS) and not os.listdir(DOWNLOADS):
        os.rmdir(DOWNLOADS)
    print()
    if failed:
        print("Not installed: " + ", ".join(failed) + ". Run this again to retry; finished parts are skipped.")
        return 1
    print("Instrument library ready.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
