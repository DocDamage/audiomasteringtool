# Architecture

## Phase 0 baseline

AudioMasteringTool is split into three runtime concerns from day one:

1. **`amt_core`** — portable C++20 domain/DSP/analysis contracts. No UI dependency.
2. **`amt_worker`** — isolated process boundary for ML inference and later high-risk/heavy jobs.
3. **`audiomasteringtool`** — Windows-first desktop shell.

Future VST3 and web hosts must consume the same mastering-domain contracts rather than duplicating mastering logic.

## Processing boundary

Heavy ML and separation/restoration work belongs outside the audio/UI process. The worker must be crash-isolated, cancellable, restartable, and version-negotiated. Phase 0 uses a health/smoke executable; a framed IPC transport is introduced before production inference.

## Inference abstraction

`IInferenceBackend` prevents ONNX Runtime, CUDA, WebGPU, or cloud providers from leaking into domain code. Models are addressed by stable model IDs. Device/provider selection is policy, not business logic.

## Instrument-aware direction

The core contracts already represent time-local instrument detections with exact-label, family, confidence, and time range. Later phases add hierarchical taxonomy, source masks, characteristics, and interaction analysis.

## Audio architecture rules

- Source audio remains canonical.
- Separation is not automatically substituted for the original mix.
- Source-guided masks may drive processing on the original signal.
- Full stem reconstruction is permitted only when expected repair benefit exceeds separation-artifact risk.
- Internal processing remains lossless/high precision.
- Loudness and true-peak implementations must conform to ITU-R BS.1770-5 validation fixtures before release.
