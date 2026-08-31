# Roadmap

The authoritative roadmap is `AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`.

## 0.0.1 — Phase 0: foundation

Implemented:

- C++20 core/domain contracts
- Windows shell + CLI
- streaming WAV/FLAC proof
- worker isolation + IPC proof
- inference-provider abstraction
- CI/test/benchmark infrastructure
- dependency/model governance
- JUCE/ONNX/WebGPU/codec technical spikes
- 60-minute streaming regression

## Next — Phase 1: audio foundation and standards-compliant metering

Implement production audio I/O, playback, waveform caching, transparent export, resampling, BS.1770-5 loudness/true-peak metering, spectral analysis, and stereo/phase analysis.

Do not expand the UI into a DAW. The product remains focused on analyze → repair → master → compare → export.
