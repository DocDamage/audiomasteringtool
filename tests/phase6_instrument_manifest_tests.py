import json
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT = Path(__file__).parents[1] / "scripts" / "validate_instrument_corpus_manifest.py"
HASH = "a" * 64

def invoke(rows):
    with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as file:
        for row in rows:
            file.write(json.dumps(row) + "\n")
        path = Path(file.name)
    result = subprocess.run([sys.executable, str(SCRIPT), str(path)], capture_output=True, text=True)
    path.unlink()
    return result.returncode

base = {"corpus_id":"openmic-2018", "split":"train", "item_id":"one", "audio_sha256":HASH,
        "labels":["kick"], "license_provenance":"CC-BY-4.0", "artist_group":"artist-a", "usage":"training"}
assert invoke([base]) == 0
bad_hash = dict(base, audio_sha256="placeholder")
assert invoke([bad_hash]) == 1
test_leak = dict(base, split="test", item_id="two")
assert invoke([base, test_leak]) == 1
