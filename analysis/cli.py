"""Dispatcher used by run.cmd: `cli.py analyze_track|tag_samples <args...>`."""
import json
import os
import runpy
import sys

TOOLS = ("analyze_track", "tag_samples", "encode_mp3", "separate_stems", "index_user_instruments", "dspreset_to_sfz", "index_midi_clips")

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in TOOLS:
        print(json.dumps({"error": "usage: run.cmd analyze_track|tag_samples <args>"}))
        sys.exit(2)
    tool = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    sys.argv = [os.path.join(here, tool + ".py")] + sys.argv[2:]
    runpy.run_path(sys.argv[0], run_name="__main__")
