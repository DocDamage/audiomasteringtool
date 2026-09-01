# Third-party notices

Third-party dependencies are tracked in `third_party/dependencies.json`. Every production dependency must have a pinned version/provenance decision and its redistribution obligations must remain reviewable.

## libsndfile 1.2.2

Phase 1 uses libsndfile through runtime dynamic loading behind the production `ICodecService` boundary for the controlled WAV/AIFF/FLAC foundation. The Phase 0 bit-exact compatibility adapter remains present while the migration is validated.

libsndfile is LGPL-2.1-or-later. Distribution of a runtime with the product must include the required LGPL notices/source/relinking compliance appropriate to the selected packaging model. The verified Windows x64 artifact and SHA-256 are recorded in `third_party/dependencies.json`.

## libebur128 1.2.6

Phase 1 pins libebur128 commit `67b33abe1558160ed76ada1322329b0e9e058b02` and builds it as a static dependency of `amt_analysis` for BS.1770-family loudness/sample-peak/true-peak measurement and EBU loudness range.

libebur128 is MIT licensed. Its license text is retained at `third_party/licenses/libebur128-COPYING.txt`.

## ONNX Runtime 1.29.0

Used by the optional inference spike. ONNX Runtime is MIT licensed. Production redistribution must retain its required copyright/license notice.

## JUCE 8.0.15

Evaluation spike only; not included in the default application path. JUCE 8 modules are dual-licensed under AGPLv3 and a commercial JUCE licence. Production adoption remains blocked until the project explicitly selects a compatible licence path.

## FFmpeg

Not included in Phase 1. It remains the broad-codec candidate for MP3/AAC/M4A/OGG/Opus where libsndfile is not the selected production path. If adopted, the exact build configuration and license obligations must be recorded; GPL/nonfree components must never be silently inherited by a distribution build.
