# AudioMasteringTool

An autonomous, instrument-aware audio finishing and mastering engine.

> **Drop a track in. Get a finished master.**

AudioMasteringTool is designed to analyze a stereo mix, identify specific instruments and production elements where confidence allows, diagnose problems, perform mix repair when necessary, create two distinct mastered candidates, recommend one, provide loudness-matched A/B auditioning, accept natural-language revisions, and export release-ready audio.

The first production target is a Windows standalone application. VST3, web, macOS, and Linux follow after the standalone mastering engine is validated.

## Authoritative specification

See [`AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`](./AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md).

## Current status

**Phase 1 — production audio foundation and standards-compliant analysis implemented.**

Phase 0 remains intact underneath the production layer. Phase 1 adds:

- framework-independent planar float32 audio buffers
- streaming decoder/encoder contracts with seek/tell, cancellation, progress, metadata, tags, and codec capability discovery
- controlled production WAV, AIFF, and FLAC support through runtime-loaded libsndfile
- replaceable stateful high-quality windowed-sinc resampling
- multiresolution waveform min/max/RMS peak caching
- transparent streaming export with decoded-audio comparison
- deterministic TPDF dither only when export intentionally quantizes/reduces to integer PCM
- Windows native playback transport with play, pause, resume, stop, seek, and synchronized playhead
- BS.1770-5 Annex 1 programme loudness analysis using a pinned libebur128 implementation
- integrated, momentary, and short-term loudness; LRA; sample peak; true peak; crest factor; PLR; and time-series statistics
- deterministic FFT spectrum, centroid, rolloff, and band-energy analysis
- stereo correlation, band-limited M/S width, mono-compatibility, and phase-instability indicators
- NaN/Inf, DC-offset, clipping/full-scale-run, head/tail silence, and channel-imbalance analysis
- a Windows desktop Phase 1 shell that loads audio, runs analysis asynchronously, displays waveform/metrics, controls playback, seeks, cancels work, and performs transparent export
- CLI production commands for probe, analyze, export, compare, and playback while retaining Phase 0 regression commands
- EBU-derived loudness/true-peak conformance tests, format round trips, sample-rate matrices, long-file tests, and dependency/model policy validation

Detailed acceptance criteria and validation live in [`docs/PHASE_1_ACCEPTANCE.md`](docs/PHASE_1_ACCEPTANCE.md). Phase 0 evidence remains in [`docs/PHASE_0_ACCEPTANCE.md`](docs/PHASE_0_ACCEPTANCE.md) and [`docs/PHASE_0_SPIKES.md`](docs/PHASE_0_SPIKES.md).

## Build

### Windows

Requirements: Visual Studio with the C++ desktop workload and a CMake version that supports the installed Visual Studio generator. The checked-in Windows preset auto-detects the installed Visual Studio; current CI validates the current `windows-latest` image.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Build Release with:

```powershell
cmake --build --preset windows-release
```

The desktop executable is produced under `build/windows-msvc/src/app/<Configuration>/audiomasteringtool.exe`.

### Portable smoke build

```bash
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

Portable builds validate the framework-independent audio/analysis layers and transport contracts. Native playback is intentionally Windows-first in Phase 1.

## Production codec runtime

The mastering core does not link directly to libsndfile. `amt_codec` loads the compatible runtime dynamically behind `ICodecService`, so the codec backend remains replaceable.

The controlled Phase 1 production set is:

- WAV
- AIFF
- FLAC

MP3, AAC/M4A, OGG, and Opus remain behind the reviewed broad-codec/licensing decision and are not advertised as Phase 1 production support.

## Phase 1 CLI

```text
amt_cli --version
amt_cli codec-status
amt_cli probe input.wav
amt_cli analyze input.wav
amt_cli export input.wav output.wav
amt_cli export input.wav output.wav --sample-rate 44100 --bits 24
amt_cli compare input.wav output.wav --tolerance 1e-7
amt_cli play input.wav
```

Phase 0 compatibility commands remain available:

```text
amt_cli rerender input.wav output.wav
amt_cli verify input.wav output.wav
```

## Metering scope

Phase 1 measures conventional programme layouts on the ITU-R BS.1770-5 Annex 1 path and validates key EBU Tech 3341 minimum-requirement cases in CI. It does **not** claim advanced/object-based Annex 3/4 coverage.

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

**Phase 2 — deterministic mastering baseline.**

The next layer builds tested mastering DSP on top of the Phase 1 audio/analysis foundation: gain, EQ, dynamic EQ, compression, multiband dynamics, transient shaping, saturation, M/S/stereo tools, clipping, limiting, dither, a processing graph, offline renderer, heuristic planner, two deterministic master candidates, and loudness-matched A/B.

Instrument-classification ML, kick/808 intelligence, source separation, source-aware repair, natural-language revision, references, profiles, and translation modeling remain later phases according to the authoritative specification.
