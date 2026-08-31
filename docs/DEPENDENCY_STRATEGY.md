# Phase 0 dependency strategy

This document records the selected dependency direction required by the Phase 0 exit criteria.

## Core

`amt_core` stays C++20 and dependency-light. UI frameworks, codec APIs, model runtimes, GPU APIs, and cloud SDKs are adapters around the core rather than dependencies of mastering-domain logic.

## Baseline audio I/O

**Selected for Phase 0:** dynamically loaded libsndfile 1.2.2 for the required integer-PCM WAV/FLAC CLI proof.

Why:

- no compile/link dependency is required for a clean clone to build;
- its stable C ABI is sufficient for a narrow streaming proof;
- WAV and FLAC can be decoded/written incrementally;
- the CLI can verify decoded sample equality after re-rendering.

This adapter is deliberately narrow. Phase 1 replaces it with the production codec abstraction.

**FFmpeg:** evaluated, not bundled in Phase 0. It remains the likely broad-codec fallback for MP3/AAC/M4A/Opus/etc. only after a reviewed build configuration and redistribution process are defined. GPL/nonfree build switches must never be inherited accidentally.

## Inference

**Selected direction:** ONNX Runtime behind `IInferenceBackend`.

- CPU is mandatory fallback.
- NVIDIA CUDA is the first accelerated desktop target.
- native WebGPU remains a future cross-vendor path evaluated per model.
- cloud workers share the same model/job concepts but are not part of the core ABI.

Phase 0 pins the inference spike to ONNX Runtime 1.29.0 and provides reproducible CPU/CUDA spike code. Production integration occurs after model selection.

## Desktop/plugin host

JUCE 8.0.15 passes the technical feasibility evaluation for a thin standalone/plugin host, but it is **not** pulled into the default build in Phase 0. Its dual AGPL/commercial licensing requires an explicit distribution/licensing decision before adoption in a closed-source public build.

The native Win32 shell proves that the core is host-independent. If JUCE licensing is acceptable, a later host layer may use JUCE; otherwise iPlug2/direct platform/VST3 wrappers remain viable because mastering logic is isolated.

## IPC

Phase 0 uses newline-delimited versioned JSON over stdin/stdout as an executable IPC proof. Production transport will retain the envelope semantics but may use named pipes/shared memory for control/binary audio. Giant tensor/audio payloads are never serialized as JSON.
