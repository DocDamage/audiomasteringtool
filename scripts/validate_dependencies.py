import re
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

# Reconcile ONNX Runtime version with CMakeLists.txt if pinned
cmake_path = root / "CMakeLists.txt"
if cmake_path.exists():
    cmake_content = cmake_path.read_text(encoding="utf-8")
    ort_match = re.search(r'set\(AMT_ORT_VERSION\s+"([^"]+)"\)', cmake_content)
    if ort_match:
        cmake_ort_ver = ort_match.group(1)
        ort_dep = next((d for d in items if d["name"] == "ONNX Runtime"), None)
        assert ort_dep, "ONNX Runtime dependency missing in manifest"
        assert ort_dep["version"] == cmake_ort_ver, (
            f"ONNX Runtime version mismatch: manifest={ort_dep['version']} vs CMakeLists.txt={cmake_ort_ver}"
        )

print(f"dependency manifest valid and reconciled: {len(items)} entries")
