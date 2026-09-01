#!/usr/bin/env python3
"""Exercise the Phase 1 streaming decoder/analyzer/exporter on a long source."""
import argparse
import math
import os
import struct
import subprocess
import tempfile
import wave


def run(*args):
    print("+", *map(str, args), flush=True)
    subprocess.run([str(x) for x in args], check=True)


def generate(path, minutes):
    sample_rate = 8000
    frames = int(minutes * 60 * sample_rate)
    block_frames = 4096
    phase = 0
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(sample_rate)
        remaining = frames
        while remaining:
            count = min(block_frames, remaining)
            data = bytearray(count * 2)
            for i in range(count):
                value = int(12000 * math.sin(2 * math.pi * 220 * phase / sample_rate))
                struct.pack_into("<h", data, i * 2, value)
                phase += 1
            out.writeframesraw(data)
            remaining -= count
        out.writeframes(b"")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cli")
    parser.add_argument("--minutes", type=float, default=60.0)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="amt-phase1-long-") as tmp:
        source = os.path.join(tmp, "source.wav")
        rendered = os.path.join(tmp, "rendered.wav")
        generate(source, args.minutes)
        run(args.cli, "probe", source)
        run(args.cli, "analyze", source)
        run(args.cli, "export", source, rendered)
        run(args.cli, "compare", source, rendered, "--tolerance", "1e-7")
        print(
            "phase1 long stream: ok",
            f"minutes={args.minutes}",
            f"source_bytes={os.path.getsize(source)}",
            f"rendered_bytes={os.path.getsize(rendered)}",
        )


if __name__ == "__main__":
    main()
