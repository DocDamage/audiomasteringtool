#!/usr/bin/env python3
"""Decode locked A/B listener responses using a private Phase 5 blind key.

Output JSONL is directly consumable by `phase5_mode1_calibration.py`.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

DAMAGE_FLAGS = (
    "artifactAudible",
    "transientDamage",
    "lowEndImproved",
    "monoCompatibilityWorse",
    "stereoImageWorse",
    "sectionConsistencyWorse",
    "tonalSideEffect",
)


def load_key(path: Path) -> dict[str, dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schemaVersion") != 1:
        raise ValueError("blind key has unsupported schema")
    trials = data.get("trials")
    if not isinstance(trials, list):
        raise ValueError("blind key trials must be an array")
    result: dict[str, dict[str, Any]] = {}
    for trial in trials:
        if not isinstance(trial, dict):
            raise ValueError("blind key contains a non-object trial")
        trial_id = trial.get("trialId")
        if not isinstance(trial_id, str) or not trial_id:
            raise ValueError("blind key trialId must be non-empty")
        if trial_id in result:
            raise ValueError(f"duplicate blind key trialId: {trial_id}")
        if trial.get("A") not in {"guided", "stereo"} or trial.get("B") not in {"guided", "stereo"}:
            raise ValueError(f"blind key trial has invalid A/B mapping: {trial_id}")
        if trial["A"] == trial["B"]:
            raise ValueError(f"blind key trial maps both candidates identically: {trial_id}")
        result[trial_id] = trial
    return result


def load_responses(path: Path) -> list[dict[str, Any]]:
    responses: list[dict[str, Any]] = []
    seen: set[str] = set()
    with path.open("r", encoding="utf-8") as handle:
        for line_number, raw in enumerate(handle, 1):
            text = raw.strip()
            if not text or text.startswith("#"):
                continue
            parsed = json.loads(text)
            if not isinstance(parsed, dict):
                raise ValueError(f"line {line_number}: response must be an object")
            trial_id = parsed.get("trialId")
            if not isinstance(trial_id, str) or not trial_id:
                raise ValueError(f"line {line_number}: trialId must be non-empty")
            if trial_id in seen:
                raise ValueError(f"line {line_number}: duplicate response for {trial_id}")
            seen.add(trial_id)
            preference = parsed.get("blindPreference")
            if preference not in {"A", "B", "tie"}:
                raise ValueError(f"line {line_number}: blindPreference must be A, B, or tie")
            category = parsed.get("category")
            if not isinstance(category, str) or not category:
                raise ValueError(f"line {line_number}: category must be filled in")
            if not isinstance(parsed.get("alreadyGood"), bool):
                raise ValueError(f"line {line_number}: alreadyGood must be boolean")
            for flag in DAMAGE_FLAGS:
                if not isinstance(parsed.get(flag), bool):
                    raise ValueError(f"line {line_number}: {flag} must be boolean")
            responses.append(parsed)
    if not responses:
        raise ValueError("response file is empty")
    return responses


def decoded_preference(preference: str, key: dict[str, Any]) -> str:
    if preference == "tie":
        return "tie"
    return key[preference]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("responses", type=Path)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    try:
        key = load_key(args.key)
        responses = load_responses(args.responses)

        decoded: list[dict[str, Any]] = []
        for response in responses:
            trial_id = response["trialId"]
            trial = key.get(trial_id)
            if trial is None:
                raise ValueError(f"response trial is missing from blind key: {trial_id}")
            decoded.append({
                "trackId": trial_id,
                "category": response["category"],
                "alreadyGood": response["alreadyGood"],
                "sourceGuidanceApplied": True,
                "blindPreference": decoded_preference(response["blindPreference"], trial),
                "artifactAudible": response["artifactAudible"],
                "transientDamage": response["transientDamage"],
                "lowEndImproved": response["lowEndImproved"],
                "monoCompatibilityWorse": response["monoCompatibilityWorse"],
                "stereoImageWorse": response["stereoImageWorse"],
                "sectionConsistencyWorse": response["sectionConsistencyWorse"],
                "tonalSideEffect": response["tonalSideEffect"],
                "notes": response.get("notes", ""),
                "modelName": trial.get("modelName", ""),
                "modelVersion": trial.get("modelVersion", ""),
                "evidenceMode": trial.get("evidenceMode", ""),
            })

        with args.output.open("w", encoding="utf-8") as handle:
            for record in decoded:
                handle.write(json.dumps(record, sort_keys=True) + "\n")
        print(f"decoded calibration corpus: {args.output} ({len(decoded)} record(s))")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"blind response decode failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
