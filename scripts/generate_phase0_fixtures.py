#!/usr/bin/env python3
"""Generate tiny, legally safe Phase 0 audio fixtures without network access."""
import argparse
import base64
import math
from pathlib import Path
import struct
import wave

FLAC_PCM16_BASE64 = (
    "ZkxhQwAAACICQAJAAACdAAEpAfQA8AAAAyB58RJTBAvWwQ8/ZJZYRBIThAAALAwAAABMYXZmNjEuNy4xMDMBAAAAFAAAAGVuY29kZXI9TGF2ZjYxLjcuMTAz//gkCADKTgAADTsY5iGfJl4mlCI7GdXlmam7gm1zZSjgPsqhiP7hgCu1qKEwEYWvI7IwTCwlFZDEerCMCYhKEYjU6iYEYSExa5BhDCYoUJKLudUhRIopaYgYRlEWEiwhiMjlosEwikRhBibkwmFISSxBhHMTKJQEYIxNNOlkiQkkprtaihMBGFryOyMEwsJRWQxHqwjAmIShGI1OomBGEhMWuQYQwmKFCSi7nVIUSKKWmIGEZRFhIsIYjI5aLBMIpEYQYm5MJhSEksQYRzEyiUBGCMTTTpZIkJJKa7WooTARha8jsjBMLCUVkMR6sIwJiEoRiNTqJgRhITFrkGEMJihQkou51SFEiilpiBhGURYSLCGIyOWiwTCKRGEGJuTCYUhJLEGEc/7u//hkCAHf5U7cqNkd2ivfsukL9RsCcw+D5Zmouf52e0wpEU6CAcS+0gBKQsPy9ZmQQxmIkvU/f9MQgjCQI7F5XpbCQJgmESZXo8L9sNASCNrDwXqszGQNBkIudSrzkUTDMIxNPSu/W5GIMxmaxVLy4U3EYQyCJlwsFhboQyBsI0kPguL4mGwRiE070fHqkhiCEYZSFh+XrMyCGMAuGA=="
)

parser = argparse.ArgumentParser()
parser.add_argument("output_dir", nargs="?", default="build/phase0-fixtures")
args = parser.parse_args()
out = Path(args.output_dir)
out.mkdir(parents=True, exist_ok=True)

wav_path = out / "sine_pcm16.wav"
rate = 8000
frames = 800
with wave.open(str(wav_path), "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(rate)
    data = bytearray(frames * 2)
    for i in range(frames):
        sample = int(10000 * math.sin(2 * math.pi * 440 * i / rate))
        struct.pack_into("<h", data, i * 2, sample)
    wav.writeframes(data)

flac_path = out / "sine_pcm16.flac"
flac_path.write_bytes(base64.b64decode(FLAC_PCM16_BASE64))
print(wav_path)
print(flac_path)
