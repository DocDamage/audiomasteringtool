# AudioMasteringTool

An autonomous, instrument-aware audio finishing and mastering engine.

> **Drop a track in. Get a finished master.**

AudioMasteringTool is designed to analyze a stereo mix, identify specific instruments and production elements where confidence allows, diagnose problems, perform mix repair when necessary, create two distinct mastered candidates, recommend one, provide loudness-matched A/B auditioning, accept natural-language revisions, and export release-ready audio.

The first production target is a Windows standalone application. VST3, web, macOS, and Linux follow after the standalone mastering engine is validated.

## Authoritative specification

See [`AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`](./AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md).

## Current status

**Phase 0 — foundation implemented.**

The repository now contains:

- portable C++20 `amt_core` library
- Windows desktop shell
- `amt_cli` baseline WAV/FLAC streaming proof
- isolated `amt_worker` with versioned stdio IPC smoke protocol
- inference-provider abstraction
- instrument-aware/time-local analysis contracts
- benchmark harness
- Windows + portable build/test CI
- synthetic WAV/FLAC round-trip fixtures
- scheduled/manual 60-minute streaming regression
- ONNX Runtime CPU/CUDA C++ spike
- native WebGPU evaluation
- JUCE 8.0.15 host feasibility spike
- dependency/model governance and licence manifests
- architecture decisions, security policy, and coding conventions

Detailed Phase 0 evidence is in [`docs/PHASE_0_ACCEPTANCE.md`](docs/PHASE_0_ACCEPTANCE.md) and [`docs/PHASE_0_SPIKES.md`](docs/PHASE_0_SPIKES.md).

## Build

### Windows

Requirements: Visual Studio with the C++ desktop workload (VS 2022 or newer) and a CMake version that supports the installed Visual Studio generator. The checked-in Windows preset auto-detects the installed Visual Studio; current CI validates VS 2026.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Build Release with:

```powershell
cmake --build --preset windows-release
```

### Portable smoke build

```bash
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

## Phase 0 CLI

The default build does **not** link libsndfile. `amt_cli` loads a compatible libsndfile runtime dynamically only when WAV/FLAC codec commands are used.

```text
amt_cli --version
amt_cli codec-status
amt_cli probe input.wav
amt_cli rerender input.wav output.wav
amt_cli verify input.wav output.wav
```

The Phase 0 audio proof supports integer-PCM WAV/FLAC (16/24/32-bit), streams fixed-size blocks, preserves format/sample rate/channel count, and verifies decoded PCM equality after re-rendering. The production codec/export layer is a Phase 1 concern.

## Core principles

- automatic by default
- specific-instrument identification when confidence supports it
- honest hierarchical fallback when it does not
- time-aware detection and repair
- dedicated kick/808 intelligence
- preserve intentional grit/clipping/saturation/sample character
- use source separation only when the benefit exceeds artifact risk
- keep original stereo source canonical
- two genuinely different mastering candidates
- loudness-matched Original/A/B auditioning
- optional references and personal sound profiles
- natural-language mastering revisions
- translation-aware mastering
- lossless/high-precision internal pipeline
- local-first CPU/GPU/cloud abstraction

## Next milestone

Phase 1 implements the production audio foundation: codec abstraction, streaming reader/writer, playback/transport, waveform cache, BS.1770-5 loudness/true-peak metering, spectral/stereo analysis, resampling, and transparent export.
