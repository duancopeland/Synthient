"""Encode a WAV file to MP3 with LAME (lameenc).

Usage: run.cmd encode_mp3 <input.wav> <output.mp3> [--bitrate 320]
Prints one JSON object: {"ok": true, "path": ..., "bitrate": ..., "seconds": ...} or {"error": ...}.
"""
import argparse
import json
import sys
import wave

import numpy as np


def read_wav(path):
    with wave.open(path, "rb") as w:
        channels, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(frames)

    if width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.int16)
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        v = np.where(v & 0x800000, v - 0x1000000, v)
        data = (v >> 8).astype(np.int16)
    elif width == 4:
        data = (np.frombuffer(raw, dtype="<i4") >> 16).astype(np.int16)
    else:
        raise ValueError(f"unsupported sample width {width}")

    return data.reshape(-1, channels), rate


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--bitrate", type=int, default=320)
    args = parser.parse_args()

    try:
        import lameenc

        pcm, rate = read_wav(args.input)
        if pcm.shape[1] > 2:
            pcm = pcm[:, :2]

        encoder = lameenc.Encoder()
        encoder.set_bit_rate(args.bitrate)
        encoder.set_in_sample_rate(rate)
        encoder.set_channels(pcm.shape[1])
        encoder.set_quality(2)  # 2 = high quality, 7 = fast

        mp3 = encoder.encode(np.ascontiguousarray(pcm).tobytes()) + encoder.flush()
        with open(args.output, "wb") as f:
            f.write(mp3)

        print(json.dumps({"ok": True, "path": args.output, "bitrate": args.bitrate,
                          "seconds": round(pcm.shape[0] / rate, 2)}))
    except Exception as e:  # report any failure as JSON for the app
        print(json.dumps({"error": f"MP3 encode failed: {e}"}))
        sys.exit(1)


if __name__ == "__main__":
    main()
