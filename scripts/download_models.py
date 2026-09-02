#!/usr/bin/env python3
"""
Downloads and verifies model weights defined in models/registry.json.
"""
import argparse
import hashlib
import json
import os
import sys
import urllib.request
from pathlib import Path

def compute_sha256(file_path: Path) -> str:
    h = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()

def download_file(url: str, dest: Path, expected_sha256: str) -> bool:
    dest.parent.mkdir(parents=True, exist_ok=True)
    temp_dest = dest.with_suffix(".tmp")
    
    if dest.exists():
        print(f"Checking existing file: {dest} ...")
        current_sha = compute_sha256(dest)
        if current_sha.lower() == expected_sha256.lower():
            print(f"  [OK] File is already installed and verified.")
            return True
        else:
            print(f"  [WARN] SHA-256 mismatch (found {current_sha[:12]}, expected {expected_sha256[:12]}). Re-downloading...")

    print(f"Downloading from {url} to {dest} ...")
    
    last_percent = -1

    def reporthook(count, block_size, total_size):
        nonlocal last_percent
        if total_size > 0:
            percent = min(100, int(count * block_size * 100 / total_size))
            if percent == last_percent:
                return
            last_percent = percent
            mb_done = (count * block_size) / (1024 * 1024)
            mb_total = total_size / (1024 * 1024)
            sys.stdout.write(f"\r  Progress: {percent:3d}% ({mb_done:.1f}/{mb_total:.1f} MB)")
            sys.stdout.flush()

    try:
        urllib.request.urlretrieve(url, temp_dest, reporthook=reporthook)
        print()
    except Exception as e:
        print(f"\n  [ERROR] Download failed: {e}")
        if temp_dest.exists():
            temp_dest.unlink()
        return False

    downloaded_sha = compute_sha256(temp_dest)
    if downloaded_sha.lower() != expected_sha256.lower():
        print(f"  [ERROR] Checksum verification failed!")
        print(f"    Expected: {expected_sha256}")
        print(f"    Actual:   {downloaded_sha}")
        temp_dest.unlink()
        return False

    temp_dest.replace(dest)
    print(f"  [OK] Verified and saved to {dest}")
    return True

def main():
    parser = argparse.ArgumentParser(
        description="Download hash-pinned model artifacts for internal evaluation."
    )
    parser.add_argument(
        "--allow-research-only",
        action="store_true",
        help="explicitly allow a model that is not approved for commercial use or redistribution",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    registry_path = root / "models" / "registry.json"
    if not registry_path.exists():
        print(f"Error: Registry not found at {registry_path}", file=sys.stderr)
        return 1

    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    models = registry.get("models", [])
    print(f"AudioMasteringTool Model Downloader")
    print(f"Found {len(models)} model definition(s) in registry.")

    known_urls = {
        "htdemucs-onnx-fp16weights": "https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/d54ed9eb60e258ea82131c6ee14578628816456a/htdemucs_fp16weights.onnx?download=true"
    }

    success = True
    for model in models:
        model_id = model["id"]
        sha256 = model["sha256"]
        artifact_rel = model["artifact"]
        dest_path = root / "models" / artifact_rel
        url = known_urls.get(model_id)

        production_allowed = (
            model.get("commercialUseReviewed") is True
            and model.get("commercialUse") is True
            and model.get("redistributionReviewed") is True
            and model.get("redistributionAllowed") is True
        )
        if not production_allowed and not args.allow_research_only:
            print(
                f"\nRefusing research-only model {model_id}; pass "
                "--allow-research-only for an internal, non-distributed evaluation."
            )
            success = False
            continue

        print(f"\nProcessing model: {model_id} (v{model['version']})")
        if not url:
            print(f"  No download URL configured for model {model_id}.")
            continue

        if not download_file(url, dest_path, sha256):
            success = False

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
