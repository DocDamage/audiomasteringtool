#!/usr/bin/env python3
"""Evaluate Phase 5 source-guided Mode 1 listening/damage evidence.

The script intentionally does not invent acceptance thresholds. A policy JSON must
be supplied before it can emit an approval decision or change the model registry.
This keeps `automaticMode1Approved` tied to explicit, reviewable evidence.

Input is JSONL, one blind-listening record per track/listening pass. Example:
{
  "trackId": "mix-001",
  "category": "clean-balanced",
  "alreadyGood": true,
  "sourceGuidanceApplied": true,
  "blindPreference": "stereo",
  "artifactAudible": false,
  "transientDamage": false,
  "lowEndImproved": false,
  "monoCompatibilityWorse": false,
  "stereoImageWorse": false,
  "sectionConsistencyWorse": false,
  "tonalSideEffect": false
}
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

PREFERENCE_VALUES = {"guided", "stereo", "tie"}
DAMAGE_FLAGS = (
    "artifactAudible",
    "transientDamage",
    "monoCompatibilityWorse",
    "stereoImageWorse",
    "sectionConsistencyWorse",
    "tonalSideEffect",
)


@dataclass(frozen=True)
class Policy:
    minimum_already_good: int
    minimum_targeted: int
    maximum_already_good_damage_rate: float
    maximum_artifact_rate: float
    minimum_targeted_guided_win_rate: float
    maximum_targeted_guided_loss_rate: float


def require_bool(record: dict[str, Any], key: str, line: int) -> bool:
    value = record.get(key)
    if not isinstance(value, bool):
        raise ValueError(f"line {line}: {key} must be boolean")
    return value


def require_string(record: dict[str, Any], key: str, line: int) -> str:
    value = record.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"line {line}: {key} must be a non-empty string")
    return value


def validate_record(record: Any, line: int) -> dict[str, Any]:
    if not isinstance(record, dict):
        raise ValueError(f"line {line}: record must be an object")
    require_string(record, "trackId", line)
    require_string(record, "category", line)
    require_bool(record, "alreadyGood", line)
    require_bool(record, "sourceGuidanceApplied", line)
    preference = require_string(record, "blindPreference", line)
    if preference not in PREFERENCE_VALUES:
        raise ValueError(
            f"line {line}: blindPreference must be one of {sorted(PREFERENCE_VALUES)}"
        )
    for key in DAMAGE_FLAGS:
        require_bool(record, key, line)
    if "lowEndImproved" in record and not isinstance(record["lowEndImproved"], bool):
        raise ValueError(f"line {line}: lowEndImproved must be boolean when present")
    return record


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, raw in enumerate(handle, 1):
            text = raw.strip()
            if not text or text.startswith("#"):
                continue
            try:
                parsed = json.loads(text)
            except json.JSONDecodeError as exc:
                raise ValueError(f"line {line_number}: invalid JSON: {exc}") from exc
            records.append(validate_record(parsed, line_number))
    if not records:
        raise ValueError("calibration corpus is empty")
    return records


def rate(numerator: int, denominator: int) -> float | None:
    if denominator == 0:
        return None
    return numerator / denominator


def wilson_interval(successes: int, total: int, z: float = 1.959963984540054) -> list[float] | None:
    if total <= 0:
        return None
    p = successes / total
    z2 = z * z
    denominator = 1.0 + z2 / total
    center = (p + z2 / (2.0 * total)) / denominator
    spread = (
        z
        * math.sqrt((p * (1.0 - p) + z2 / (4.0 * total)) / total)
        / denominator
    )
    return [max(0.0, center - spread), min(1.0, center + spread)]


def is_damage(record: dict[str, Any]) -> bool:
    return any(record[key] for key in DAMAGE_FLAGS)


def preference_counts(records: Iterable[dict[str, Any]]) -> Counter[str]:
    return Counter(record["blindPreference"] for record in records)


def metrics(records: list[dict[str, Any]]) -> dict[str, Any]:
    applied = [record for record in records if record["sourceGuidanceApplied"]]
    already_good = [record for record in applied if record["alreadyGood"]]
    targeted = [record for record in applied if not record["alreadyGood"]]

    all_preferences = preference_counts(applied)
    good_preferences = preference_counts(already_good)
    targeted_preferences = preference_counts(targeted)

    damaged_good = sum(is_damage(record) for record in already_good)
    audible_artifacts = sum(record["artifactAudible"] for record in applied)
    targeted_wins = targeted_preferences["guided"]
    targeted_losses = targeted_preferences["stereo"]

    category_counts = Counter(record["category"] for record in applied)
    category_damage = Counter(
        record["category"] for record in applied if is_damage(record)
    )

    return {
        "recordsTotal": len(records),
        "sourceGuidanceApplied": len(applied),
        "alreadyGoodCount": len(already_good),
        "targetedCount": len(targeted),
        "preferences": dict(all_preferences),
        "alreadyGoodPreferences": dict(good_preferences),
        "targetedPreferences": dict(targeted_preferences),
        "alreadyGoodDamageCount": damaged_good,
        "alreadyGoodDamageRate": rate(damaged_good, len(already_good)),
        "alreadyGoodDamageRate95CI": wilson_interval(damaged_good, len(already_good)),
        "artifactCount": audible_artifacts,
        "artifactRate": rate(audible_artifacts, len(applied)),
        "artifactRate95CI": wilson_interval(audible_artifacts, len(applied)),
        "targetedGuidedWinRate": rate(targeted_wins, len(targeted)),
        "targetedGuidedWinRate95CI": wilson_interval(targeted_wins, len(targeted)),
        "targetedGuidedLossRate": rate(targeted_losses, len(targeted)),
        "targetedGuidedLossRate95CI": wilson_interval(targeted_losses, len(targeted)),
        "categoryCounts": dict(sorted(category_counts.items())),
        "categoryDamageCounts": dict(sorted(category_damage.items())),
    }


def bounded_rate(value: Any, key: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"policy {key} must be numeric")
    value = float(value)
    if not math.isfinite(value) or value < 0.0 or value > 1.0:
        raise ValueError(f"policy {key} must be between 0 and 1")
    return value


def positive_int(value: Any, key: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"policy {key} must be a positive integer")
    return value


def load_policy(path: Path) -> Policy:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("policy root must be an object")
    return Policy(
        minimum_already_good=positive_int(data.get("minimumAlreadyGood"), "minimumAlreadyGood"),
        minimum_targeted=positive_int(data.get("minimumTargeted"), "minimumTargeted"),
        maximum_already_good_damage_rate=bounded_rate(
            data.get("maximumAlreadyGoodDamageRate"), "maximumAlreadyGoodDamageRate"
        ),
        maximum_artifact_rate=bounded_rate(
            data.get("maximumArtifactRate"), "maximumArtifactRate"
        ),
        minimum_targeted_guided_win_rate=bounded_rate(
            data.get("minimumTargetedGuidedWinRate"), "minimumTargetedGuidedWinRate"
        ),
        maximum_targeted_guided_loss_rate=bounded_rate(
            data.get("maximumTargetedGuidedLossRate"), "maximumTargetedGuidedLossRate"
        ),
    )


def evaluate_policy(report: dict[str, Any], policy: Policy) -> dict[str, Any]:
    checks: dict[str, bool] = {
        "minimumAlreadyGood": report["alreadyGoodCount"] >= policy.minimum_already_good,
        "minimumTargeted": report["targetedCount"] >= policy.minimum_targeted,
        "maximumAlreadyGoodDamageRate": (
            report["alreadyGoodDamageRate"] is not None
            and report["alreadyGoodDamageRate"] <= policy.maximum_already_good_damage_rate
        ),
        "maximumArtifactRate": (
            report["artifactRate"] is not None
            and report["artifactRate"] <= policy.maximum_artifact_rate
        ),
        "minimumTargetedGuidedWinRate": (
            report["targetedGuidedWinRate"] is not None
            and report["targetedGuidedWinRate"] >= policy.minimum_targeted_guided_win_rate
        ),
        "maximumTargetedGuidedLossRate": (
            report["targetedGuidedLossRate"] is not None
            and report["targetedGuidedLossRate"] <= policy.maximum_targeted_guided_loss_rate
        ),
    }
    return {"approved": all(checks.values()), "checks": checks}


def set_registry_approval(registry_path: Path, model_id: str, approved: bool) -> None:
    data = json.loads(registry_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schemaVersion") != 2:
        raise ValueError("registry must use schemaVersion 2")
    models = data.get("models")
    if not isinstance(models, list):
        raise ValueError("registry models must be an array")
    matches = [model for model in models if isinstance(model, dict) and model.get("id") == model_id]
    if len(matches) != 1:
        raise ValueError(f"registry must contain exactly one model with id {model_id!r}")
    matches[0]["automaticMode1Approved"] = approved
    temporary = registry_path.with_suffix(registry_path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    temporary.replace(registry_path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("corpus", type=Path, help="JSONL blind-listening/damage corpus")
    parser.add_argument("--policy", type=Path, help="explicit acceptance policy JSON")
    parser.add_argument("--output", type=Path, help="write report JSON")
    parser.add_argument("--registry", type=Path, help="registry to update after an approved report")
    parser.add_argument("--model-id", help="model id to approve when --registry is supplied")
    parser.add_argument(
        "--apply-approval",
        action="store_true",
        help="set automaticMode1Approved=true only when all supplied policy checks pass",
    )
    args = parser.parse_args()

    try:
        records = load_jsonl(args.corpus)
        report: dict[str, Any] = {"metrics": metrics(records), "approval": None}
        if args.policy:
            policy = load_policy(args.policy)
            report["approval"] = evaluate_policy(report["metrics"], policy)

        if args.apply_approval:
            if not args.policy:
                raise ValueError("--apply-approval requires --policy")
            if not args.registry or not args.model_id:
                raise ValueError("--apply-approval requires --registry and --model-id")
            if not report["approval"]["approved"]:
                raise ValueError("calibration policy did not pass; registry approval was not changed")
            set_registry_approval(args.registry, args.model_id, True)
            report["registryUpdated"] = True

        text = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(text, encoding="utf-8")
        else:
            sys.stdout.write(text)
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"phase5 calibration failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
