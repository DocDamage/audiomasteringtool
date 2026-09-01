# Phase 1 acceptance — production audio foundation and metering

Phase 1 is accepted only when the implementation and automated validation below are green. This document deliberately separates implemented capability from later product/ML phases.

## Production audio foundation

- [x] Framework-independent planar float32 `AudioBuffer`.
- [x] Interleaved ↔ planar conversion.
- [x] Streaming decoder/encoder contracts.
- [x] Audio metadata and codec capability discovery.
- [x] Cooperative cancellation and progress callbacks.
- [x] Streaming seek/tell support.
- [x] Controlled WAV/AIFF/FLAC production backend.
- [x] Replaceable streaming high-quality windowed-sinc resampler.
- [x] Multiresolution waveform min/max/RMS peak cache.
- [x] Transparent export preserving source sample rate/channels by default.
- [x] TPDF dither only on intentional integer bit-depth reduction.
- [x] Decoded-audio comparison independent of container format.

## Playback / transport

- [x] Play/pause/resume/seek/stop transport contract.
- [x] Synchronized frame playhead from the decoder.
- [x] Bounded queued Windows audio output.
- [x] Decoder access isolated from the device rendering worker.
- [x] No model inference on the playback path.

Windows is the Phase 1 native playback target. Portable builds compile the transport contracts but intentionally report native output unsupported until the later platform phases.

## BS.1770-5 / loudness baseline

The Phase 1 scope is conventional programme audio covered by the BS.1770-5 Annex 1 path. Advanced loudspeaker layouts and object-based Annex 3/4 measurement are explicit later work.

Implemented:

- [x] K-weighted/gated integrated loudness through pinned libebur128.
- [x] Momentary loudness.
- [x] Short-term loudness.
- [x] Loudness range.
- [x] Sample peak.
- [x] True peak.
- [x] Crest factor.
- [x] Peak-to-loudness ratio.
- [x] Time-series/section-scale loudness statistics.
- [x] Conventional mono/stereo/3.0/quad/5.0/5.1 channel maps.

Conformance tests generated in code:

- EBU Tech 3341 Test 1 — stereo 1 kHz, -23 dBFS peak, 20 s → M/S/I = -23.0 ±0.1 LUFS.
- EBU Tech 3341 Test 6 — conventional 5.0 channel weighting → I = -23.0 ±0.1 LUFS.
- Inter-sample true-peak stress — fs/4 sine, 1.41 linear amplitude, 45° phase → approximately +3 dBTP within published true-peak tolerance.

Passing these tests is a required CI gate; they are not replaced by approximate hand-written loudness formulas.

## Deterministic analysis

- [x] FFT spectral distribution.
- [x] Spectral centroid.
- [x] 85% spectral rolloff.
- [x] Fixed low/bass/low-mid/mid/upper-mid/high energy ratios.
- [x] Stereo correlation.
- [x] M/S-derived low/mid/high width.
- [x] Mono fold-down level delta.
- [x] Negative-correlation window fraction.
- [x] NaN/Inf detection.
- [x] DC offset.
- [x] clipping/full-scale run detection.
- [x] head/tail silence.
- [x] channel imbalance.

Instrument classification, genre models, structural ML and source separation are deliberately not part of Phase 1.

## Automated exit gates

CI must prove:

1. Debug and Release builds on Windows.
2. Portable debug build/test on Linux.
3. Phase 0 regressions remain green.
4. Phase 1 unit/conformance tests pass.
5. Production WAV load/analyze/export works.
6. 24-bit WAV → AIFF → decoded comparison passes.
7. 24-bit WAV → FLAC → decoded comparison passes.
8. Sample-rate conversion matrix covers 44.1/48/96 kHz.
9. Intentional 24→16 bit-depth export succeeds with dither path.
10. Long-file streaming regression exercises probe → analyze → export → decoded comparison without loading the entire file into RAM.
11. Dependency and model registries remain valid.

## Explicit non-claims

Phase 1 does not claim:

- instrument-identification ML
- kick/808 intelligence
- stem/source-aware repair
- deterministic mastering DSP chains
- two-master generation/A-B
- immersive/object-based BS.1770 Annex 3/4 coverage
- macOS/Linux native playback
- production MP3/AAC/M4A/OGG/Opus codec support

Those remain in their scheduled later phases.
