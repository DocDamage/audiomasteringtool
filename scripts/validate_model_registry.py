#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
registry_path = root / "models" / "registry.json"
data = json.loads(registry_path.read_text(encoding="utf-8"))
assert data.get("schemaVersion") == 1, "unsupported model registry schema"
assert isinstance(data.get("models"), list), "models must be a list"
required = {
    "id", "version", "source", "artifact", "sha256", "runtime",
    "codeLicense", "weightsLicense", "commercialUse", "attribution",
    "ramMb", "vramMb", "securityReview"
}
for model in data["models"]:
    missing = sorted(required - set(model))
    assert not missing, f"model {model.get('id', '<unknown>')} missing: {missing}"
    assert len(model["sha256"]) == 64, f"model {model['id']} sha256 must be 64 hex chars"
print(f"model registry valid: {len(data['models'])} model(s)")
