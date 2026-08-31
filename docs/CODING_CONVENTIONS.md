# Coding conventions

## C++

- C++20 baseline until a later ADR changes it.
- RAII for ownership; raw owning pointers are prohibited except inside narrow ABI/PIMPL boundaries.
- No exceptions across C/plugin/worker boundaries.
- No model inference, filesystem traversal, network I/O, allocation-heavy work, or blocking calls on a future real-time audio thread.
- Domain code must not include JUCE, Win32 UI, ONNX Runtime, cloud SDK, or codec implementation headers.
- Use explicit units in names (`sample_rate`, `frames`, `seconds`, `lufs`) rather than ambiguous numeric values.
- Confidence values are normalized to `[0, 1]` and validated.
- Serialized contracts require schema/protocol versions.
- User-visible subjective scores must be described as model assessments, not objective truth.

## Formatting / warnings

- Warnings are enabled at `/W4` on MSVC and `-Wall -Wextra -Wpedantic` elsewhere.
- New warnings should be fixed rather than globally disabled.
- clang-format/clang-tidy are planned CI gates as the codebase grows.

## Audio correctness

- The original source remains canonical.
- Internal intermediates are lossless/high precision.
- Never normalize, dither, resample, or change channel topology implicitly.
- Dither occurs only at an intentional integer-bit-depth reduction boundary.
- A mastering change requires measurable regression checks plus listening validation.

## Tests

Every new DSP/domain behavior requires deterministic automated tests where practical. Audio fixtures must be synthetic, owned, public-domain, or otherwise cleared for repository use.
