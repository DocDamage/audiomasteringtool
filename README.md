# AudioMasteringTool

An autonomous, instrument-aware audio finishing and mastering engine.

> **Drop a track in. Get a finished master.**

AudioMasteringTool analyzes a stereo mix, identifies specific instruments and production elements where confidence allows, diagnoses problems, performs mix repair when necessary, creates two distinct mastered candidates, recommends one, provides loudness-matched A/B auditioning, accepts natural-language revisions, simulates playback translation, and exports release-ready audio.

The first production target is a complete Windows standalone application. VST3/CLAP/ARA, web, macOS, and Linux follow after the standalone mastering engine.

## Authoritative Specification

See [`AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`](./AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md).

## Current Status: Windows 1.0 Release Baseline Complete

**Phases 0 through 15 are fully implemented, tested, and validated with 100% CTest pass rates (35 of 35 suites).**

### Key Capabilities

1. **Production Audio & Metering Foundation (Phases 0–1)**:
   - Framework-independent planar float32 audio buffers.
   - Dynamic streaming codec engine for WAV, AIFF, and FLAC with metadata, tags, and progress callbacks.
   - ITU-R BS.1770-5 / EBU R128 programme loudness analysis (integrated LUFS, momentary, short-term, LRA, true peak, crest factor, PLR).
   - Multiresolution min/max/RMS waveform cache and high-quality sinc resampling.
   - Windows WASAPI native playback engine with smooth seeking and playhead synchronization.

2. **Mastering DSP & Automated Decision Planning (Phases 2–3, 9)**:
   - Complete high-precision mastering processor library: linear/minimum phase EQ, dynamic EQ, opto/VCA/clean compressors, multiband dynamics, transient shaping, analog-modeled saturation, M/S widening, clipping, and true-peak limiting.
   - Decision Engine v2: Multi-band macro-dynamics, spectral balance analysis, mix health diagnostic report, and generation of two distinct candidates (**Master A: Recommended** and **Master B: Preservation/Alternative**).
   - Loudness-matched real-time comparison engine (Original vs Master A vs Master B).

3. **Instrument Taxonomy & Interaction Intelligence (Phases 5–8)**:
   - 28-class hierarchical instrument taxonomy with confidence calibration and safe fallback.
   - Source-guided stem analysis with HTDemucs ONNX inference and stem reconstruction safety gates.
   - Low-end intelligence and pairwise kick/808 phase/frequency collision detection with damage-guarded repair.
   - Mix restoration & repair tools: de-clipping, de-clicking, resonance hum removal, stereo phase stabilization, and DC offset correction.

4. **Reference Mastering & MySound Profiles (Phase 10)**:
   - Target curve matching against single/multiple reference tracks.
   - User sound profile extraction ("MySound") capturing personalized tonal and dynamic curves.

5. **Natural-Language Revision Engine (Phase 11)**:
   - Rule-based natural language intent parser translating instructions (e.g. *"punchier kick"*, *"tame harsh highs"*, *"warmer vocals"*) into deterministic parametric DSP adjustments.
   - Constraint-bounded plan editor and explanation generator.

6. **Playback Translation Simulation (Phase 12)**:
   - 9 acoustic simulation filters: Studio Monitors, Headphones, Earbuds, Phone Speaker, Laptop Speaker, Bluetooth Speaker, Car Audio, Mono System, Club PA.
   - Instrument survival metrics and translation scoring across listening environments.

7. **Preference Learning & Album Cohesion (Phases 13–14)**:
   - Continuous user preference bias vector with recency weighting and JSONL persistence.
   - Multi-track album batch queue with proportional loudness and dynamic cohesion planning.

8. **Hardening, Desktop GUI, & Installer (Phase 15)**:
   - Win32 desktop application shell with interactive action bars, waveform visualizer, natural language revision box, translation selector, album batch runner, and settings modal.
   - `SettingsManager`, `CacheManager` (with disk budget enforcement and cache purge), and `ModelManager`.
   - Automated Inno Setup 6 installer (`installer/audiomasteringtool_setup.iss`) and release packaging script (`scripts/package_release.ps1`).

---

## Build & Test Instructions

### Windows (MSVC C++20)

Requirements: Visual Studio 2022+ with C++ Desktop Workload and CMake 3.24+.

```powershell
# Configure build
cmake -B build/win -S .

# Build Release
cmake --build build/win --config Release

# Run complete 35-suite test suite
ctest --test-dir build/win -C Release --output-on-failure
```

### Packaging Windows Release

To build, test, stage, and generate the standalone installer:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1
```

---

## Command Line Interface (`amt_cli`)

```text
amt_cli --version
amt_cli codec-status
amt_cli probe input.wav
amt_cli analyze input.wav
amt_cli master input.wav -o output_dir/
amt_cli revise input.wav -o output_dir/ --prompt "punchier drums, tame high end"
amt_cli translate input.wav --profile phone_speaker
amt_cli batch file1.wav file2.wav file3.wav -o album_out/
amt_cli export input.wav output.wav --sample-rate 44100 --bits 24
amt_cli compare input.wav output.wav --tolerance 1e-7
amt_cli play input.wav
```

---

## Core Architecture Principles

- **Automatic by default**: Drop a track in and get a finished master.
- **Instrument-aware**: Time-localized instrument detection and interaction diagnostics.
- **Preserve Character**: Respect intentional saturation, clipping, and sample texture.
- **Original is Canonical**: Source stereo mix is never replaced with blind separation without safety-gated benefit verification.
- **Two Distinct Candidates**: Master A (target competitive release) and Master B (preservation-focused).
- **Loudness-Matched Auditioning**: Seamless A/B comparison without volume bias.
- **Lossless & High Precision**: Float32 planar pipeline with deterministic TPDF dither on final quantization.

---

## Roadmap Ahead

- **Phase 16 — VST3 / ARA-capable Architecture (`1.1`)**
- **Phase 17 — Web Application (`1.2`)**
- **Phase 18 — macOS (`1.3`) and Linux (`1.4`)**
