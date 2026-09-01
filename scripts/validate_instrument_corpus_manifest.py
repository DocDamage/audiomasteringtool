#!/usr/bin/env python3
"""Validate the immutable Phase 6 corpus manifest before a training run."""
import hashlib
import json
import sys
from collections import defaultdict
from pathlib import Path

REQUIRED = {"corpus_id", "split", "item_id", "audio_sha256", "labels",
            "license_provenance", "artist_group", "usage"}
SPLITS = {"train", "validation", "test", "negative"}

def fail(message: str) -> None:
    raise ValueError(message)

def main() -> int:
    if len(sys.argv) != 2:
        print("usage: validate_instrument_corpus_manifest.py <manifest.jsonl>", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    seen_items: set[tuple[str, str]] = set()
    groups: dict[str, set[str]] = defaultdict(set)
    rows = 0
    try:
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            row = json.loads(line)
            missing = REQUIRED - row.keys()
            if missing:
                fail(f"line {line_number}: missing fields {sorted(missing)}")
            if row["split"] not in SPLITS:
                fail(f"line {line_number}: unsupported split")
            if not isinstance(row["labels"], list) or not all(isinstance(v, str) and v for v in row["labels"]):
                fail(f"line {line_number}: labels must be non-empty strings")
            digest = row["audio_sha256"]
            if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest.lower()):
                fail(f"line {line_number}: audio_sha256 must be a SHA-256 hex digest")
            if not row["license_provenance"].strip() or not row["artist_group"].strip():
                fail(f"line {line_number}: license and leakage group are required")
            key = (row["corpus_id"], row["item_id"])
            if key in seen_items:
                fail(f"line {line_number}: duplicate corpus/item identity")
            seen_items.add(key)
            groups[row["artist_group"]].add(row["split"])
            rows += 1
        if rows == 0:
            fail("manifest contains no records")
        leaked = sorted(group for group, splits in groups.items()
                        if "test" in splits and ("train" in splits or "validation" in splits))
        if leaked:
            fail("artist/source leakage across test and train/validation: " + ", ".join(leaked))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"invalid instrument corpus manifest: {error}", file=sys.stderr)
        return 1
    print(f"valid instrument corpus manifest: {rows} records")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
