# Dependency strategy

## Core

`amt_core` stays C++20 and dependency-light. UI frameworks, codec APIs, model runtimes, GPU APIs, cloud SDKs and standards libraries are adapters around the core rather than dependencies of mastering-domain logic.

## Audio I/O

**Phase 1 production foundation:** runtime-loaded libsndfile 1.2.2 behind `ICodecService`.

The advertised Phase 1 production set is intentionally controlled:

- WAV
- AIFF
- FLAC

The service provides streaming float decode/write, seek/tell, metadata, capability discovery and a replaceable decoder/encoder boundary. Phase 0's integer-PCM WAV/FLAC helper remains only as a regression/bit-exact proof during migration.

**FFmpeg:** evaluated but not bundled in Phase 1. It remains the likely broad-codec fallback for MP3/AAC/M4A/OGG/Opus/etc. only after a reviewed build configuration and redistribution process are defined. GPL/nonfree build switches must never be inherited accidentally.

## Metering

**Selected:** libebur128 1.2.6 pinned to commit `67b33abe1558160ed76ada1322329b0e9e058b02`.

It is MIT licensed and is built only into `amt_analysis`. The wrapper exposes AudioMasteringTool-owned result types and is conformance-tested with generated EBU Tech 3341 vectors.

The Phase 1 standards claim is intentionally scoped to conventional BS.1770-5 Annex 1 programme audio. Advanced/object-based Annex 3/4 layouts require a separately validated implementation before they can be advertised.

## Resampling

Phase 1 provides a replaceable stateful windowed-sinc implementation with anti-alias regression tests. The interface permits later substitution with a separately licensed/resolved production resampler if listening and benchmark evaluation justify it.

## Playback

The Windows-first standalone foundation uses the native WinMM waveOut API behind `IAudioOutputDevice`; it introduces no third-party runtime dependency. Other OS output adapters are deferred to their platform milestones.

## Inference

**Selected direction:** ONNX Runtime behind `IInferenceBackend`.

- CPU is mandatory fallback.
- NVIDIA CUDA is the first accelerated desktop target.
- native WebGPU remains a future cross-vendor path evaluated per model.
- cloud workers share the same model/job concepts but are not part of the core ABI.

Phase 0 pins the inference spike to ONNX Runtime 1.29.0 and provides reproducible CPU/CUDA spike code. Production integration occurs after model selection. CUDA runtime validation still requires a self-hosted Windows + NVIDIA runner.

## Desktop/plugin host

JUCE 8.0.15 passes the technical feasibility evaluation for a thin standalone/plugin host, but its dual AGPL/commercial licensing requires an explicit distribution/licensing decision before adoption in a closed-source public build.

The native Win32 shell and Phase 1 native playback prove that the core is host-independent. If JUCE licensing is acceptable, a later host layer may use JUCE; otherwise iPlug2/direct platform/VST3 wrappers remain viable because mastering logic is isolated.

## IPC

Phase 0 uses newline-delimited versioned JSON over stdin/stdout as an executable IPC proof. Production transport will retain the envelope semantics but may use named pipes/shared memory for control/binary audio. Giant tensor/audio payloads are never serialized as JSON.
