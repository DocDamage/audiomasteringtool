#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / "third_party" / "dependencies.json"
data = json.loads(path.read_text(encoding="utf-8"))
assert data.get("schemaVersion") == 1
items = data.get("dependencies")
assert isinstance(items, list) and items, "dependency manifest must not be empty"
required = {"name", "version", "purpose", "source", "integration", "license", "productionStatus"}
seen = set()
for item in items:
    missing = sorted(required - set(item))
    assert not missing, f"{item.get('name', '<unknown>')} missing {missing}"
    key = (item["name"].lower(), item["version"])
    assert key not in seen, f"duplicate dependency {key}"
    seen.add(key)
    for field, value in item.items():
        if field.lower().endswith("sha256"):
            assert isinstance(value, str) and len(value) == 64
            int(value, 16)
print(f"dependency manifest valid: {len(items)} entries")
