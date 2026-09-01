#!/usr/bin/env python3
"""Create randomized blind A/B listening bundles from Phase 5 calibration manifests.

The public bundle contains only anonymous A/B audio plus a listener response
JSONL template. A separate key file records which candidate is stereo/guided.
Do not give the key file to listeners until responses are locked.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import random
import shutil
import sys
from pathlib import Path
from typing import Any

AUDIO_EXTENSIONS = {".wav", ".wave", ".aif", ".aiff", ".flac"}
CANDIDATE_FLAGS = (
    "artifactAudible",
    "transientDamage",
    "lowEndImproved",
    "monoCompatibilityWorse",
    "stereoImageWorse",
    "sectionConsistencyWorse",
    "tonalSideEffect",
)


def load_manifest(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schemaVersion") != 1:
        raise ValueError(f"unsupported calibration manifest: {path}")
    if data.get("guidedCandidateRendered") is not True:
        raise ValueError(f"manifest has no guided calibration candidate: {path}")
    for field in (
        "source",
        "stereoMasterA",
        "guidedMasterA",
        "stereoBlindAudition",
        "guidedBlindAudition",
        "modelName",
        "modelVersion",
    ):
        value = data.get(field)
        if not isinstance(value, str) or not value:
            raise ValueError(f"manifest field {field} is missing: {path}")
    reference = data.get("auditionReferenceLufs")
    stereo_gain = data.get("stereoAuditionGainDb")
    guided_gain = data.get("guidedAuditionGainDb")
    for label, value in (
        ("auditionReferenceLufs", reference),
        ("stereoAuditionGainDb", stereo_gain),
        ("guidedAuditionGainDb", guided_gain),
    ):
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            raise ValueError(f"manifest field {label} must be numeric: {path}")
    if stereo_gain > 1.0e-9 or guided_gain > 1.0e-9:
        raise ValueError(f"blind audition gains must be attenuation-only: {path}")
    return data


def safe_audio(path_text: str, label: str) -> Path:
    path = Path(path_text)
    if path.suffix.lower() not in AUDIO_EXTENSIONS:
        raise ValueError(f"{label} is not a supported audio path: {path}")
    if not path.is_file():
        raise ValueError(f"{label} does not exist: {path}")
    return path


def stable_trial_id(manifest: Path, source: str, model: str, version: str) -> str:
    digest = hashlib.sha256(
        (str(manifest.resolve()) + "\0" + source + "\0" + model + "\0" + version).encode("utf-8")
    ).hexdigest()
    return digest[:20]


def copy_candidate(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Do not preserve source timestamps or other filesystem metadata in the
    # listener bundle. The anonymous filename should be the only visible identity.
    shutil.copyfile(source, destination)


def blank_candidate_rating() -> dict[str, bool]:
    return {flag: False for flag in CANDIDATE_FLAGS}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifests", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=None,
                        help="optional reproducible randomization seed")
    args = parser.parse_args()

    try:
        args.output.mkdir(parents=True, exist_ok=True)
        public_root = args.output / "listener"
        private_root = args.output / "private"
        public_root.mkdir(parents=True, exist_ok=True)
        private_root.mkdir(parents=True, exist_ok=True)

        rng = random.Random(args.seed)
        key_records: list[dict[str, Any]] = []
        response_records: list[dict[str, Any]] = []

        seen_ids: set[str] = set()
        for manifest_path in args.manifests:
            manifest = load_manifest(manifest_path)
            # Blind tests must use the attenuation-only loudness-matched copies,
            # never the raw mastering renders. Otherwise a small LUFS difference
            # could bias preference toward the louder candidate.
            stereo = safe_audio(manifest["stereoBlindAudition"], "stereoBlindAudition")
            guided = safe_audio(manifest["guidedBlindAudition"], "guidedBlindAudition")
            trial_id = stable_trial_id(
                manifest_path, manifest["source"], manifest["modelName"], manifest["modelVersion"]
            )
            if trial_id in seen_ids:
                raise ValueError(f"duplicate calibration trial: {manifest_path}")
            seen_ids.add(trial_id)

            guided_is_a = bool(rng.getrandbits(1))
            a_source = guided if guided_is_a else stereo
            b_source = stereo if guided_is_a else guided
            trial_dir = public_root / trial_id
            a_path = trial_dir / ("A" + a_source.suffix.lower())
            b_path = trial_dir / ("B" + b_source.suffix.lower())
            copy_candidate(a_source, a_path)
            copy_candidate(b_source, b_path)

            key_records.append({
                "trialId": trial_id,
                "manifest": str(manifest_path.resolve()),
                "source": manifest["source"],
                "modelName": manifest["modelName"],
                "modelVersion": manifest["modelVersion"],
                "A": "guided" if guided_is_a else "stereo",
                "B": "stereo" if guided_is_a else "guided",
                "evidenceMode": manifest.get("evidenceMode"),
                "auditionReferenceLufs": manifest["auditionReferenceLufs"],
                "issues": manifest.get("issues", []),
            })

            response_records.append({
                "trialId": trial_id,
                "category": "",
                "alreadyGood": False,
                "blindPreference": "",
                "A": blank_candidate_rating(),
                "B": blank_candidate_rating(),
                "notes": "",
            })

        key_path = private_root / "blind-key.json"
        key_path.write_text(
            json.dumps({"schemaVersion": 1, "trials": key_records}, indent=2) + "\n",
            encoding="utf-8",
        )
        responses_path = public_root / "responses.jsonl"
        with responses_path.open("w", encoding="utf-8") as handle:
            for record in response_records:
                handle.write(json.dumps(record, sort_keys=True) + "\n")

        instructions = public_root / "README.txt"
        instructions.write_text(
            "Phase 5 blind source-guidance listening bundle\n\n"
            "For each trial folder, compare A and B at the supplied matched level.\n"
            "The files were generated from attenuation-only LUFS-matched audition copies.\n"
            "Do not normalize, gain-match again, inspect metadata, or open the private blind-key.json.\n"
            "Fill one responses.jsonl record per trial. blindPreference must be A, B, or tie.\n"
            "Score the A and B candidate flags separately. Do not guess which one is guided.\n"
            "Set damage/side-effect flags only when you can hear the problem reliably.\n"
            "lowEndImproved is a positive flag; the others describe audible damage/side effects.\n",
            encoding="utf-8",
        )

        print(f"blind bundle created: {args.output}")
        print(f"listener material: {public_root}")
        print(f"private key: {key_path}")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"blind bundle failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
