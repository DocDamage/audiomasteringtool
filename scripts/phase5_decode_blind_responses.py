#!/usr/bin/env python3
"""Decode locked A/B listener responses using a private Phase 5 blind key.

Output JSONL is directly consumable by `phase5_mode1_calibration.py`. Candidate
ratings are collected independently while blinded; after responses are locked,
this decoder selects the ratings that belong to the hidden guided candidate.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

CANDIDATE_FLAGS = (
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


def validate_candidate_rating(value: Any, label: str, line_number: int) -> dict[str, bool]:
    if not isinstance(value, dict):
        raise ValueError(f"line {line_number}: candidate {label} rating must be an object")
    output: dict[str, bool] = {}
    for flag in CANDIDATE_FLAGS:
        flag_value = value.get(flag)
        if not isinstance(flag_value, bool):
            raise ValueError(f"line {line_number}: {label}.{flag} must be boolean")
        output[flag] = flag_value
    return output


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
            parsed["A"] = validate_candidate_rating(parsed.get("A"), "A", line_number)
            parsed["B"] = validate_candidate_rating(parsed.get("B"), "B", line_number)
            responses.append(parsed)
    if not responses:
        raise ValueError("response file is empty")
    return responses


def decoded_preference(preference: str, key: dict[str, Any]) -> str:
    if preference == "tie":
        return "tie"
    return key[preference]


def guided_candidate_label(key: dict[str, Any]) -> str:
    if key["A"] == "guided":
        return "A"
    if key["B"] == "guided":
        return "B"
    raise ValueError("blind key has no guided candidate")


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
            guided_label = guided_candidate_label(trial)
            guided_rating = response[guided_label]
            decoded.append({
                "trackId": trial_id,
                "category": response["category"],
                "alreadyGood": response["alreadyGood"],
                "sourceGuidanceApplied": True,
                "blindPreference": decoded_preference(response["blindPreference"], trial),
                "artifactAudible": guided_rating["artifactAudible"],
                "transientDamage": guided_rating["transientDamage"],
                "lowEndImproved": guided_rating["lowEndImproved"],
                "monoCompatibilityWorse": guided_rating["monoCompatibilityWorse"],
                "stereoImageWorse": guided_rating["stereoImageWorse"],
                "sectionConsistencyWorse": guided_rating["sectionConsistencyWorse"],
                "tonalSideEffect": guided_rating["tonalSideEffect"],
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
