# Phase 1 acceptance — production audio foundation and metering

Phase 1 is accepted only when the implementation and automated validation below are green. This document deliberately separates implemented capability from later product/ML phases.

## Production audio foundation

- [x] Framework-independent planar float32 `AudioBuffer`.
- [x] Interleaved ↔ planar conversion.
- [x] Streaming decoder/encoder contracts.
- [x] Audio metadata, conventional channel-layout metadata, tags, and codec capability discovery.
- [x] Cooperative cancellation and progress callbacks.
- [x] Streaming seek/tell support.
- [x] Controlled WAV/AIFF/FLAC production backend enforced at the backend boundary.
- [x] Replaceable streaming high-quality windowed-sinc resampler.
- [x] Multiresolution waveform min/max/RMS peak cache.
- [x] Transparent export preserving source sample rate/channels by default.
- [x] Deterministic TPDF dither when export intentionally quantizes/reduces to integer PCM.
- [x] No same-depth pass-through dither.
- [x] Decoded-audio comparison independent of container format.

## Playback / transport

- [x] Play/pause/resume/seek/stop transport contract.
- [x] Synchronized frame playhead from the decoder.
- [x] Bounded queued Windows audio output.
- [x] Decoder access isolated from the device rendering worker.
- [x] No model inference on the playback path.
- [x] Transport logic testable without a physical audio endpoint through a fake output device.

Windows is the Phase 1 native playback target. Portable builds compile the transport contracts but intentionally report native output unsupported until the later platform phases.

Hosted CI does not provide a meaningful physical speaker endpoint, so CI compiles the WinMM output implementation and tests transport semantics through a fake device; real hardware playback remains part of Windows exploratory/manual validation rather than a falsely claimed hosted-CI runtime proof.

## Windows desktop Phase 1 foundation

The Windows application itself is no longer the Phase 0 blank shell.

- [x] Open WAV/AIFF/FLAC source.
- [x] Probe and display source metadata.
- [x] Run analysis asynchronously off the UI/audio callback path.
- [x] Show progress and support cooperative cancellation.
- [x] Render the multiresolution waveform cache.
- [x] Display loudness, peak, spectral, stereo/phase, and integrity metrics.
- [x] Play/pause/resume/stop mono/stereo sources.
- [x] Seek with a synchronized playhead.
- [x] Perform transparent WAV/AIFF/FLAC export.
- [x] Headless `--phase1-self-test` executes the desktop target's analyze → waveform → export → decoded-compare integration in Windows CI.

Conventional multichannel sources can be analyzed/exported; Phase 1 native playback is intentionally mono/stereo.

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
- Inter-sample true-peak stress — fs/4 sine, 1.41 linear amplitude, 45° phase → approximately +3 dBTP within the published true-peak tolerance.

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
- [x] Clipping/full-scale-run detection.
- [x] Head/tail silence.
- [x] Channel imbalance.

Instrument classification, genre models, structural ML and source separation are deliberately not part of Phase 1.

## Automated exit gates

CI must prove:

1. Debug and Release builds on Windows.
2. Portable debug build/test on Linux.
3. Phase 0 regressions remain green.
4. Phase 1 unit/conformance tests pass.
5. Windows desktop target executes the Phase 1 analyze/waveform/export integration self-test.
6. Production WAV load/analyze/export works.
7. 24-bit WAV → AIFF → decoded comparison passes.
8. 24-bit WAV → FLAC → decoded comparison passes.
9. Sample-rate conversion matrix covers 44.1/48/96 kHz.
10. Intentional 24→16 bit-depth export exercises the dither/quantization path.
11. Bounded long-file smoke exercises probe → analyze → export → decoded comparison without loading the entire file into RAM.
12. The dedicated Phase 1 long-stream workflow exercises a 60-minute source on pull requests touching the audio/analysis/app pipeline, on manual dispatch, and on the weekly schedule.
13. Dependency and model registries remain valid.

## Explicit non-claims

Phase 1 does not claim:

- instrument-identification ML
- kick/808 intelligence
- stem/source-aware repair
- deterministic mastering DSP chains
- two-master generation/A-B mastering UX
- immersive/object-based BS.1770 Annex 3/4 coverage
- macOS/Linux native playback
- production MP3/AAC/M4A/OGG/Opus codec support
- hosted-CI runtime validation of a physical Windows playback endpoint
- CUDA inference runtime validation

Those remain in their scheduled later phases or require the appropriate hardware validation environment.
