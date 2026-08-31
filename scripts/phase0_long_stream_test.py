#!/usr/bin/env python3
"""Generate a 60-minute PCM16 WAV incrementally, rerender it, and verify decoded PCM.

The fixture uses 8 kHz mono to stress duration/streaming semantics while keeping CI disk and I/O
reasonable (~58 MB per file). This is not an audio-quality benchmark; higher-rate matrices belong
in Phase 1.
"""
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
    with tempfile.TemporaryDirectory(prefix="amt-phase0-long-") as tmp:
        source = os.path.join(tmp, "source.wav")
        rendered = os.path.join(tmp, "rendered.wav")
        generate(source, args.minutes)
        run(args.cli, "probe", source)
        run(args.cli, "rerender", source, rendered)
        run(args.cli, "verify", source, rendered)
        source_bytes = os.path.getsize(source)
        rendered_bytes = os.path.getsize(rendered)
        print(f"long stream test: ok minutes={args.minutes} source_bytes={source_bytes} rendered_bytes={rendered_bytes}")


if __name__ == "__main__":
    main()
