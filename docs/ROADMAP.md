# Roadmap

The authoritative roadmap is `AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`.

## 0.0.1 — Phase 0: foundation — COMPLETE

Implemented and merged:

- C++20 core/domain contracts
- Windows shell + CLI
- streaming WAV/FLAC proof
- worker isolation + IPC proof
- inference-provider abstraction
- CI/test/benchmark infrastructure
- dependency/model governance
- JUCE/ONNX/WebGPU/codec technical spikes
- 60-minute streaming regression

## Phase 1: production audio foundation and standards-compliant metering — COMPLETE ON PR #4

Implemented:

- production streaming decoder/encoder abstractions
- controlled WAV/AIFF/FLAC codec backend
- metadata, channel layouts, tags, capability discovery, seek/tell, cancellation, and progress
- planar float32 buffers and interleaved conversion
- high-quality replaceable streaming resampler
- multiresolution waveform cache
- transparent export and decoded-audio comparison
- intentional quantization dither path
- Windows native playback transport and synchronized playhead
- Windows desktop load/analyze/waveform/transport/export foundation
- BS.1770-5 Annex 1 programme loudness/true-peak metering
- EBU-derived conformance tests
- spectrum, stereo/phase, and technical-integrity analysis
- format, sample-rate, bit-depth, long-file, and policy CI gates

Phase 1 deliberately does not claim immersive/object-based BS.1770 Annex 3/4 coverage, broad lossy codec production support, instrument ML, source separation, or mastering DSP.

## Next — Phase 2: deterministic mastering baseline

Build the first complete non-ML mastering engine on top of the validated Phase 1 audio and analysis layers.

Required Phase 2 systems:

- gain and trim stages
- parametric EQ
- dynamic EQ
- compression
- multiband dynamics
- transient shaping
- saturation
- stereo/M-S processing
- clipper
- limiter
- final dither/quantization stage
- deterministic processing graph
- offline renderer
- heuristic mastering planner
- exactly two deterministic master candidates
- recommended-vs-preservation-biased candidate policy
- loudness-matched Original/A/B auditioning
- audio-regression and quality-evaluation harnesses

The deterministic baseline is a hard requirement before later AI mastering: learned systems must prove that they improve on this reference rather than merely being more complex.

Do not expand the product into a DAW. The workflow remains focused on analyze → diagnose → repair/master → compare → export.
