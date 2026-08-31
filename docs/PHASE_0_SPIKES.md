# Phase 0 technical spike report

This report maps directly to the six mandatory technical spikes in the authoritative specification.

## 1. JUCE 8 standalone shell vs alternative host

**Result:** feasible, with a licensing gate.

- Pinned evaluation: JUCE 8.0.15, commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`.
- `spikes/juce-host` contains an optional CMake standalone shell.
- Native Win32 remains the zero-dependency Phase 0 host.
- `amt_core` stays framework-independent.
- JUCE is not vendored and must pass a commercial/AGPL licensing decision before production use.

## 2. ONNX Runtime CPU + CUDA C++ inference

**Result:** implementation path established and reproducible.

- Pinned spike runtime: ONNX Runtime 1.29.0.
- `spikes/onnx-runtime` builds a C++ inference executable and deterministic `Abs` model.
- CPU path runs through the standard provider.
- CUDA path is compile-gated with `AMT_ORT_ENABLE_CUDA` and uses the CUDA EP.
- Hosted CI can execute CPU; real CUDA runtime validation requires an NVIDIA-equipped runner and is wired as an opt-in/self-hosted job.

The production core does not depend on ONNX headers; provider details remain inside inference adapters/workers.

## 3. ONNX Runtime native WebGPU

**Result:** viable future cross-vendor path, not a Windows 1.0 blocker.

- Current native WebGPU uses Dawn and is exposed as an ONNX Runtime execution provider/plugin.
- `spikes/webgpu/README.md` records the provider/build boundary.
- Each production model must be benchmarked for operator coverage, memory, startup, and throughput before WebGPU is enabled.

## 4. Decoder strategy / FFmpeg licensing

**Result:** libsndfile selected for Phase 0 WAV/FLAC proof; FFmpeg deferred to broad-codec production work.

- libsndfile is runtime-loaded, not linked into the clean-clone build.
- FFmpeg requires an explicitly reviewed LGPL-compatible configuration if that is the selected distribution path; GPL/nonfree switches are prohibited by default.
- See ADR 0004 and `third_party/dependencies.json`.

## 5. 60-minute streaming decode/render

**Result:** implemented.

`scripts/phase0_long_stream_test.py` creates a 60-minute PCM16 WAV incrementally, re-renders it in fixed blocks through `amt_cli`, then verifies decoded PCM equality. The duration fixture is intentionally 8 kHz mono to stress long-file behavior without wasting CI disk; Phase 1 owns the full sample-rate/channel matrix.

## 6. Worker-process IPC

**Result:** implemented.

`amt_worker --stdio` implements protocol-v1 health, cancel, unsupported-command error, and shutdown envelopes. `scripts/test_worker_ipc.py` launches the real worker process and verifies request/response behavior.

Phase 0 intentionally sends control messages only. Production audio/tensors will use file/shared-memory handles rather than huge JSON payloads.
