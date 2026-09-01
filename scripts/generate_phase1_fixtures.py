#!/usr/bin/env python3
"""Generate deterministic, legally safe Phase 1 PCM fixtures without external packages."""
import argparse
import math
from pathlib import Path
import wave


def pcm24(value: int) -> bytes:
    if value < 0:
        value += 1 << 24
    return bytes((value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF))


def write_stereo_pcm24(path: Path, rate: int = 48000, seconds: int = 2) -> None:
    total = rate * seconds
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(3)
        out.setframerate(rate)
        block = bytearray()
        for frame in range(total):
            left = int(0.45 * ((1 << 23) - 1) * math.sin(2 * math.pi * 997 * frame / rate))
            right = int(0.35 * ((1 << 23) - 1) * math.sin(2 * math.pi * 1499 * frame / rate + 0.2))
            block += pcm24(left)
            block += pcm24(right)
            if len(block) >= 24576:
                out.writeframesraw(block)
                block.clear()
        if block:
            out.writeframesraw(block)
        out.writeframes(b"")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", nargs="?", default="build/phase1-fixtures")
    args = parser.parse_args()
    output = Path(args.output_dir)
    output.mkdir(parents=True, exist_ok=True)
    path = output / "tone_stereo_pcm24_48000.wav"
    write_stereo_pcm24(path)
    print(path)


if __name__ == "__main__":
    main()
