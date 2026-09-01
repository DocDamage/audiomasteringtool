#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
registry_path = root / "models" / "registry.json"
data = json.loads(registry_path.read_text(encoding="utf-8"))

assert data.get("schemaVersion") == 2, "unsupported model registry schema"
assert "activeSeparationModel" in data, "activeSeparationModel must be explicit"
assert data["activeSeparationModel"] is None or (
    isinstance(data["activeSeparationModel"], str) and data["activeSeparationModel"]
), "activeSeparationModel must be null or a non-empty model id"
assert isinstance(data.get("models"), list), "models must be a list"

required = {
    "id", "version", "task", "source", "weightProvenance", "artifact", "sha256",
    "runtime", "codeLicense", "weightsLicense", "commercialUseReviewed",
    "commercialUse", "redistributionReviewed", "redistributionAllowed", "attribution",
    "ramMb", "vramMb", "securityReview", "benchmarkRecord", "executionProviders",
    "inputSampleRate", "stemTaxonomy", "automaticMode1Approved", "onnxContract"
}
allowed_stems = {
    "vocals", "drums", "bass", "other", "kick", "snare", "percussion", "tonal"
}
allowed_providers = {"cpu", "cuda"}
ids = set()

for model in data["models"]:
    assert isinstance(model, dict), "every model entry must be an object"
    missing = sorted(required - set(model))
    assert not missing, f"model {model.get('id', '<unknown>')} missing: {missing}"

    model_id = model["id"]
    assert isinstance(model_id, str) and model_id, "model id must be non-empty"
    assert model_id not in ids, f"duplicate model id: {model_id}"
    ids.add(model_id)

    assert model["task"] == "source-separation", f"model {model_id} has unsupported task"
    assert model["runtime"] == "onnxruntime-worker-v1", f"model {model_id} has unsupported runtime"
    assert isinstance(model["sha256"], str) and len(model["sha256"]) == 64, \
        f"model {model_id} sha256 must be 64 hex chars"
    assert all(c in "0123456789abcdefABCDEF" for c in model["sha256"]), \
        f"model {model_id} sha256 contains non-hex characters"

    artifact = Path(model["artifact"])
    assert not artifact.is_absolute(), f"model {model_id} artifact must be relative"
    assert ".." not in artifact.parts, f"model {model_id} artifact must stay inside models/"

    for field in ("commercialUseReviewed", "commercialUse",
                  "redistributionReviewed", "redistributionAllowed",
                  "automaticMode1Approved"):
        assert isinstance(model[field], bool), f"model {model_id} {field} must be boolean"
    assert model["securityReview"] in {"pending", "approved", "rejected"}, \
        f"model {model_id} has invalid securityReview"
    assert isinstance(model["ramMb"], int) and model["ramMb"] >= 0
    assert isinstance(model["vramMb"], int) and model["vramMb"] >= 0

    providers = model["executionProviders"]
    assert isinstance(providers, list) and providers, f"model {model_id} needs executionProviders"
    assert len(providers) == len(set(providers)), f"model {model_id} has duplicate executionProviders"
    assert set(providers) <= allowed_providers, f"model {model_id} has unsupported execution provider"

    sample_rate = model["inputSampleRate"]
    assert isinstance(sample_rate, int) and 8000 <= sample_rate <= 384000, \
        f"model {model_id} inputSampleRate is invalid"

    stems = model["stemTaxonomy"]
    assert isinstance(stems, list) and stems, f"model {model_id} needs stemTaxonomy"
    assert len(stems) == len(set(stems)), f"model {model_id} has duplicate stem roles"
    assert set(stems) <= allowed_stems, f"model {model_id} has unsupported stem role"

    contract = model["onnxContract"]
    assert isinstance(contract, dict), f"model {model_id} onnxContract must be an object"
    contract_required = {
        "inputTensor", "outputTensor", "chunkFrames", "overlapFrames",
        "calibratedOutputConfidence", "completeReconstruction"
    }
    missing_contract = sorted(contract_required - set(contract))
    assert not missing_contract, f"model {model_id} ONNX contract missing: {missing_contract}"
    assert isinstance(contract["inputTensor"], str) and contract["inputTensor"]
    assert isinstance(contract["outputTensor"], str) and contract["outputTensor"]
    assert isinstance(contract["chunkFrames"], int) and 4096 <= contract["chunkFrames"] <= 2097152
    assert isinstance(contract["overlapFrames"], int) and contract["overlapFrames"] >= 0
    assert contract["overlapFrames"] * 2 < contract["chunkFrames"], \
        f"model {model_id} overlapFrames must be less than half chunkFrames"
    confidence = contract["calibratedOutputConfidence"]
    assert isinstance(confidence, (int, float)) and not isinstance(confidence, bool) and 0 < confidence <= 1
    assert isinstance(contract["completeReconstruction"], bool)

active = data["activeSeparationModel"]
if active is not None:
    assert active in ids, f"activeSeparationModel does not match a registry entry: {active}"
    selected = next(model for model in data["models"] if model["id"] == active)
    assert "cpu" in selected["executionProviders"], \
        "active separation model must provide a CPU fallback"

print(
    f"model registry valid: {len(data['models'])} model(s); "
    f"active separation model: {active or 'none'}"
)
