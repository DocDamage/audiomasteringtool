# Private local HTDemucs runtime evidence — 2026-09-02

Scope: private, non-distributed engineering validation. The model artifact is
ignored by Git and is not included in this evidence directory or release
packages.

## Environment

- Source commit at start: `fec56833946f7cf17b234b11ca54ed09528c67b1`
- Windows 11 Pro `10.0.26200.9168`
- CPU: Intel Core i5-14600K
- Execution provider: ONNX Runtime CPU
- ONNX Runtime: packaged/pinned `1.26.0`

## Artifact identity

- Model: `htdemucs-onnx-fp16weights`
- Revision: `d54ed9eb60e258ea82131c6ee14578628816456a`
- Bytes: `165612636`
- SHA-256: `d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a`
- Cold helper-script download: passed exact size/hash verification and atomic
  publication
- Cold application download: the Windows WinHTTP installer started from a
  nonexistent nested model store, created it, downloaded the artifact, verified
  the exact size and hash, atomically published it with no `.download` sidecar,
  and immediately completed inference
- Warm verification: existing artifact accepted without a new download

The previously recorded digest was incorrect and caused the cold download to
fail closed. The registry and trusted installer identity now match the pinned
Hugging Face artifact metadata and downloaded bytes.

The first application-level cold run also exposed a Windows-specific fresh-path
bug: `is_regular_file` reported `ERROR_PATH_NOT_FOUND` when the parent hierarchy
did not exist. The installer now treats that condition as the expected absent
artifact state; all other inspection errors still fail closed.

## CPU inference

Input was the deterministic two-second stereo PCM fixture at 48 kHz. The worker
resampled to the model's 44.1 kHz contract and returned `88200` frames for each
float32 stereo stem:

| Stem | Peak | RMS | Finite |
| --- | ---: | ---: | --- |
| drums | 0.12932798 | 0.00172326 | yes |
| bass | 0.00776694 | 0.00026707 | yes |
| other | 0.47173721 | 0.28502602 | yes |
| vocals | 0.00601242 | 0.00032367 | yes |

- Measured wall time: `7.297 s`
- Peak worker working set: `4572.2 MiB`
- Registry planning requirement: `5120 MiB`

This validates runtime geometry and numerical safety, not musical separation
quality. A representative real-track test and listening acceptance remain open.

## Cache and failure matrix

- First orchestration: provider invoked, cache miss, `10.36 s`.
- Identical second orchestration: provider not invoked, cache hit, `0.89 s`.
- Corrupt cache manifest: rejected with an explicit warning and recomputed.
- Mutated source bytes: different cache identity and recomputed.
- Missing worker: failed unavailable without partial artifacts.
- Corrupt model: failed SHA-256 verification without launching inference.
- Unwritable output path: failed directory preparation safely.
- Cancellation at worker start: worker terminated and partial artifacts removed.
- One-second timeout: worker terminated and partial artifacts removed.

Post-fix automated results: Windows Release `36/36` and portable Debug `35/35`.

Model-identity/topology cache invalidation has deterministic unit coverage but
was not repeated with additional large real-model inference runs. Missing-
internet, interrupted-download, actual disk exhaustion, and a representative
real music track remain open.
