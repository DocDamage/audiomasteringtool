# ADR 0004: Baseline codec strategy

Status: Accepted for Phase 0

## Decision

Use libsndfile 1.2.2 through a runtime-loaded C ABI adapter for the Phase 0 integer-PCM WAV/FLAC streaming proof. Do not link or bundle FFmpeg in Phase 0.

## Rationale

This satisfies the required clean-clone build and lossless WAV/FLAC CLI proof while keeping production codec licensing replaceable. The adapter streams fixed blocks and verifies decoded PCM equality.

FFmpeg remains a likely broader-format adapter later, but a production build must explicitly document its configure flags and avoid accidental GPL/nonfree components unless the project's distribution model intentionally accepts those obligations.

## Consequences

- Users/developers need a compatible libsndfile runtime to run the Phase 0 codec commands.
- Phase 0 rejects floating-point WAV and non-PCM baseline subtypes rather than silently changing them.
- Phase 1 owns the production codec abstraction and extended format matrix.
