# ADR 0006 — Phase 1 production audio and metering boundaries

- Status: accepted
- Date: 2026-08-31

## Context

Phase 0 proved streaming WAV/FLAC I/O with a deliberately narrow runtime-loaded libsndfile adapter. Phase 1 needs reusable production contracts, transparent export, seeking, cancellation/progress, waveform caching, resampling, playback, and standards-based metering without coupling mastering-domain logic to a codec or UI framework.

The authoritative baseline is ITU-R BS.1770-5. For ordinary music-file programme audio, Annex 1 covers mono/stereo and conventional 3/2 multichannel programme measurement; Annexes 3 and 4 address advanced loudspeaker and object-based systems and are not silently claimed by this phase.

## Decision

1. `amt_audio` owns framework-independent floating-point buffers, streaming resampling and waveform peak-cache primitives.
2. `amt_codec` exposes `ICodecService`, streaming decoder/encoder contracts, metadata, capability discovery, seeking, transparent export, cancellation and decoded-audio comparison.
3. libsndfile 1.2.2 is the first production codec backend but remains runtime-loaded and replaceable. Phase 1 advertises WAV, AIFF and FLAC as the controlled production set. Broad lossy codecs remain behind the future reviewed FFmpeg/other backend decision.
4. `amt_analysis` owns deterministic analysis. A pinned libebur128 1.2.6 dependency provides the validated loudness/true-peak core for conventional BS.1770 programme layouts; AudioMasteringTool wraps the results and adds timeline, crest factor and PLR statistics.
5. Metering must pass generated conformance vectors derived from EBU Tech 3341, including the 1 kHz -23 dBFS programme-loudness case, 5.0 weighting case and an inter-sample true-peak case.
6. `amt_playback` is isolated from analysis/inference. Windows standalone playback uses a bounded WinMM `waveOut` queue in Phase 1; model inference is never called from its rendering path.
7. Dither is applied only when intentionally reducing integer bit depth. Same-depth/pass-through export adds no dither.

## Consequences

- The mastering core remains host-independent and usable by future standalone, VST3 and web adapters.
- Long files remain chunked rather than requiring contiguous whole-track RAM.
- Exact immersive/object loudness is explicitly deferred rather than mislabeled as BS.1770-5 complete coverage.
- FFmpeg licensing remains a separate gate before broad lossy format support is advertised.

## References

- ITU-R BS.1770-5: https://www.itu.int/rec/R-REC-BS.1770-5-202311-I/en
- EBU Tech 3341: https://tech.ebu.ch/publications/tech3341
- libebur128 v1.2.6: https://github.com/jiixyj/libebur128/tree/v1.2.6
