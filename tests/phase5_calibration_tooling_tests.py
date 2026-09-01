#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
MAKE_BUNDLE = SCRIPTS / "phase5_make_blind_bundle.py"
DECODE = SCRIPTS / "phase5_decode_blind_responses.py"
CALIBRATE = SCRIPTS / "phase5_mode1_calibration.py"
FLAGS = (
    "artifactAudible",
    "transientDamage",
    "lowEndImproved",
    "monoCompatibilityWorse",
    "stereoImageWorse",
    "sectionConsistencyWorse",
    "tonalSideEffect",
)


def run_script(script: Path, *args: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(script), *(str(arg) for arg in args)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


class Phase5CalibrationToolingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.audio = self.root / "audio"
        self.audio.mkdir()
        self.raw_stereo = self.audio / "stereo-master-a.wav"
        self.raw_guided = self.audio / "guided-master-a.wav"
        self.blind_stereo = self.audio / "stereo-blind.wav"
        self.blind_guided = self.audio / "guided-blind.wav"
        for index, path in enumerate(
            (self.raw_stereo, self.raw_guided, self.blind_stereo, self.blind_guided), 1
        ):
            path.write_bytes((f"dummy-audio-{index}").encode("ascii"))

    def tearDown(self) -> None:
        self.temp.cleanup()

    def manifest_data(self) -> dict[str, object]:
        return {
            "schemaVersion": 1,
            "source": str(self.audio / "source.wav"),
            "modelName": "test-model",
            "modelVersion": "test-version",
            "evidenceMode": "source-guided-stereo",
            "sourceEstimatesAnalyzed": True,
            "guidedCandidateRendered": True,
            "stereoMasterA": str(self.raw_stereo),
            "guidedMasterA": str(self.raw_guided),
            "auditionReferenceLufs": -14.2,
            "stereoAuditionGainDb": -0.2,
            "guidedAuditionGainDb": 0.0,
            "stereoBlindAudition": str(self.blind_stereo),
            "guidedBlindAudition": str(self.blind_guided),
            "issues": [],
        }

    def write_manifest(self, data: dict[str, object] | None = None) -> Path:
        path = self.root / "calibration-manifest.json"
        path.write_text(json.dumps(data or self.manifest_data()) + "\n", encoding="utf-8")
        return path

    def bundle(
        self, manifest: Path, output_name: str = "bundle"
    ) -> subprocess.CompletedProcess[str]:
        return run_script(
            MAKE_BUNDLE,
            manifest,
            "--output",
            self.root / output_name,
            "--seed",
            1234,
        )

    def test_bundle_rejects_missing_blind_audition_field(self) -> None:
        data = self.manifest_data()
        del data["stereoBlindAudition"]
        result = self.bundle(self.write_manifest(data))
        self.assertEqual(result.returncode, 2)
        self.assertIn("stereoBlindAudition", result.stderr)

    def test_bundle_rejects_positive_or_nonfinite_gain_metadata(self) -> None:
        positive = self.manifest_data()
        positive["guidedAuditionGainDb"] = 0.1
        result = self.bundle(self.write_manifest(positive), "positive")
        self.assertEqual(result.returncode, 2)
        self.assertIn("attenuation-only", result.stderr)

        nonfinite = self.manifest_data()
        nonfinite["auditionReferenceLufs"] = float("nan")
        result = self.bundle(self.write_manifest(nonfinite), "nonfinite")
        self.assertEqual(result.returncode, 2)
        self.assertIn("finite numeric", result.stderr)

    def test_bundle_rejects_missing_candidate_and_malformed_manifest(self) -> None:
        self.blind_stereo.unlink()
        result = self.bundle(self.write_manifest(), "missing")
        self.assertEqual(result.returncode, 2)
        self.assertIn("does not exist", result.stderr)

        malformed = self.root / "malformed.json"
        malformed.write_text("[]\n", encoding="utf-8")
        result = self.bundle(malformed, "malformed")
        self.assertEqual(result.returncode, 2)
        self.assertIn("unsupported calibration manifest", result.stderr)

    def test_bundle_rejects_raw_master_reuse_and_identical_candidates(self) -> None:
        raw_reuse = self.manifest_data()
        raw_reuse["stereoBlindAudition"] = str(self.raw_stereo)
        result = self.bundle(self.write_manifest(raw_reuse), "raw-reuse")
        self.assertEqual(result.returncode, 2)
        self.assertIn("must not reuse raw stereoMasterA", result.stderr)

        identical = self.manifest_data()
        identical["guidedBlindAudition"] = str(self.blind_stereo)
        result = self.bundle(self.write_manifest(identical), "identical")
        self.assertEqual(result.returncode, 2)
        self.assertIn("must be distinct files", result.stderr)

    def test_bundle_is_blind_and_decoder_recovers_guided_candidate_ratings(self) -> None:
        bundle_root = self.root / "bundle"
        result = self.bundle(self.write_manifest())
        self.assertEqual(result.returncode, 0, result.stderr)

        listener = bundle_root / "listener"
        private = bundle_root / "private"
        key_path = private / "blind-key.json"
        response_path = listener / "responses.jsonl"
        self.assertTrue(key_path.is_file())
        self.assertFalse((listener / "blind-key.json").exists())
        self.assertTrue((listener / "README.txt").is_file())

        key = json.loads(key_path.read_text(encoding="utf-8"))
        self.assertEqual(len(key["trials"]), 1)
        trial = key["trials"][0]
        trial_dir = listener / trial["trialId"]
        self.assertTrue((trial_dir / "A.wav").is_file())
        self.assertTrue((trial_dir / "B.wav").is_file())

        response = json.loads(response_path.read_text(encoding="utf-8").strip())
        self.assertEqual(set(response["A"]), set(FLAGS))
        self.assertEqual(set(response["B"]), set(FLAGS))
        guided_label = "A" if trial["A"] == "guided" else "B"
        stereo_label = "B" if guided_label == "A" else "A"
        response["category"] = "targeted-low-end"
        response["alreadyGood"] = False
        response["blindPreference"] = guided_label
        response[guided_label]["lowEndImproved"] = True
        response[stereo_label]["transientDamage"] = True
        response_path.write_text(json.dumps(response) + "\n", encoding="utf-8")

        decoded_path = self.root / "decoded.jsonl"
        decoded = run_script(
            DECODE,
            response_path,
            "--key",
            key_path,
            "--output",
            decoded_path,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        record = json.loads(decoded_path.read_text(encoding="utf-8").strip())
        self.assertEqual(record["blindPreference"], "guided")
        self.assertTrue(record["lowEndImproved"])
        self.assertFalse(record["transientDamage"])
        self.assertTrue(record["sourceGuidanceApplied"])

    def calibration_record(
        self,
        track_id: str,
        *,
        already_good: bool,
        preference: str = "guided",
        damage: bool = False,
    ) -> dict[str, object]:
        return {
            "trackId": track_id,
            "category": "already-good" if already_good else "targeted",
            "alreadyGood": already_good,
            "sourceGuidanceApplied": True,
            "blindPreference": preference,
            "artifactAudible": damage,
            "transientDamage": False,
            "lowEndImproved": not already_good,
            "monoCompatibilityWorse": False,
            "stereoImageWorse": False,
            "sectionConsistencyWorse": False,
            "tonalSideEffect": False,
        }

    def write_corpus(self) -> Path:
        records = [
            self.calibration_record("good-1", already_good=True, preference="tie"),
            self.calibration_record("good-2", already_good=True),
            self.calibration_record("target-1", already_good=False),
            self.calibration_record("target-2", already_good=False),
        ]
        path = self.root / "corpus.jsonl"
        path.write_text(
            "".join(json.dumps(record) + "\n" for record in records),
            encoding="utf-8",
        )
        return path

    def write_policy(self, *, minimum_targeted: int = 2) -> Path:
        policy = {
            "minimumAlreadyGood": 2,
            "minimumTargeted": minimum_targeted,
            "maximumAlreadyGoodDamageRate": 0.0,
            "maximumArtifactRate": 0.0,
            "minimumTargetedGuidedWinRate": 0.5,
            "maximumTargetedGuidedLossRate": 0.5,
        }
        path = self.root / f"policy-{minimum_targeted}.json"
        path.write_text(json.dumps(policy) + "\n", encoding="utf-8")
        return path

    def write_registry(self) -> Path:
        registry = {
            "schemaVersion": 2,
            "activeSeparationModel": "target-model",
            "models": [
                {"id": "target-model", "automaticMode1Approved": False},
                {"id": "other-model", "automaticMode1Approved": False},
            ],
        }
        path = self.root / "registry.json"
        path.write_text(json.dumps(registry, indent=2) + "\n", encoding="utf-8")
        return path

    def approvals(self, registry: Path) -> dict[str, bool]:
        data = json.loads(registry.read_text(encoding="utf-8"))
        return {
            model["id"]: model["automaticMode1Approved"]
            for model in data["models"]
        }

    def test_registry_cannot_be_approved_without_policy(self) -> None:
        corpus = self.write_corpus()
        registry = self.write_registry()
        result = run_script(
            CALIBRATE,
            corpus,
            "--registry",
            registry,
            "--model-id",
            "target-model",
            "--apply-approval",
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("requires --policy", result.stderr)
        self.assertEqual(
            self.approvals(registry),
            {"target-model": False, "other-model": False},
        )

    def test_failed_policy_cannot_change_registry(self) -> None:
        corpus = self.write_corpus()
        policy = self.write_policy(minimum_targeted=3)
        registry = self.write_registry()
        result = run_script(
            CALIBRATE,
            corpus,
            "--policy",
            policy,
            "--registry",
            registry,
            "--model-id",
            "target-model",
            "--apply-approval",
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("did not pass", result.stderr)
        self.assertEqual(
            self.approvals(registry),
            {"target-model": False, "other-model": False},
        )

    def test_passed_policy_changes_only_named_model(self) -> None:
        corpus = self.write_corpus()
        policy = self.write_policy()
        registry = self.write_registry()
        report = self.root / "report.json"
        result = run_script(
            CALIBRATE,
            corpus,
            "--policy",
            policy,
            "--output",
            report,
            "--registry",
            registry,
            "--model-id",
            "target-model",
            "--apply-approval",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(report.read_text(encoding="utf-8"))
        self.assertTrue(payload["approval"]["approved"])
        self.assertTrue(payload["registryUpdated"])
        self.assertEqual(
            self.approvals(registry),
            {"target-model": True, "other-model": False},
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
