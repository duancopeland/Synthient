"""separate_stems: split one audio file into drums/bass/vocals/other with Demucs htdemucs.

Prints progress as JSON lines ({"status": ...} / {"progress": 0..1}) and finally one JSON object
{"stems": {name: {"path", "rms_db"}}, ...}. Used by Synthient's stems window.
"""
from __future__ import annotations

import argparse
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault("TORCH_HOME", os.path.join(HERE, "models", "torch"))
os.environ.setdefault("HF_HUB_CACHE", os.path.join(HERE, "models", "hf"))
os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")

from daw_common import Timer, emit, guard_stdout, log  # noqa: E402
import analyze_track  # noqa: E402


class Progress:
    """Stands in for tqdm inside demucs.apply and reports chunk progress as JSON lines."""

    def __init__(self, iterable, **_kw):
        self.items = list(iterable)

    def __iter__(self):
        n = len(self.items)
        for i, item in enumerate(self.items):
            yield item
            emit({"progress": round((i + 1) / max(1, n), 3)})


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args(argv)
    guard_stdout()
    try:
        emit({"status": "Loading the Demucs model…"})
        try:
            import demucs.apply  # noqa: F401
        except Exception as e:  # noqa: BLE001
            raise analyze_track.StemsUnavailable(analyze_track.STEMS_NOT_INSTALLED) from e
        import demucs.apply as demucs_apply

        class ProgressModule:
            tqdm = Progress

        demucs_apply.tqdm = ProgressModule  # demucs calls tqdm.tqdm(futures, ...)
        original = demucs_apply.apply_model

        def apply_with_progress(*args, **kwargs):
            kwargs["progress"] = True
            return original(*args, **kwargs)

        analyze_track.apply_model = apply_with_progress  # in case it was imported by name
        demucs_apply.apply_model = apply_with_progress
        emit({"status": "Separating (this takes about as long as the song on a fast PC)…"})
        res = analyze_track.separate_stems(a.path, a.out_dir, Timer())
        emit({"status": "Done", **res})
        return 0
    except analyze_track.StemsUnavailable as e:
        emit({"error": str(e)})
    except Exception as e:  # noqa: BLE001
        log(traceback.format_exc())
        emit({"error": f"{type(e).__name__}: {e}"})
    return 1


if __name__ == "__main__":
    sys.exit(main())
