# Third-party notices

Phase 0's default CMake build vendors and links no third-party libraries.

Optional/runtime evaluation dependencies are tracked in `third_party/dependencies.json` and must be reviewed before distribution.

## libsndfile 1.2.2

Used only through runtime dynamic loading for the Phase 0 WAV/FLAC proof. libsndfile is LGPL-licensed. Distribution of a runtime with the product must include the required LGPL notices/source/relinking compliance appropriate to the selected packaging model.

## ONNX Runtime 1.29.0

Used by the optional inference spike. ONNX Runtime is MIT licensed. Production redistribution must retain its required copyright/license notice.

## JUCE 8.0.15

Evaluation spike only; not included in the default build. JUCE 8 modules are dual-licensed under AGPLv3 and a commercial JUCE licence. Production adoption is blocked until the project explicitly selects a compatible licence path.

## FFmpeg

Not included in Phase 0. If adopted later, the exact build configuration and license obligations must be recorded. The project will not silently enable GPL/nonfree components in a build intended for a different distribution model.
