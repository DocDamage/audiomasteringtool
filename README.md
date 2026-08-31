# AudioMasteringTool

An autonomous, instrument-aware audio finishing and mastering engine.

## Product promise

> **Drop a track in. Get a finished master.**

AudioMasteringTool is designed to analyze a stereo mix, identify specific instruments and production elements where confidence allows, diagnose problems, perform mix repair when necessary, create two distinct mastered candidates, recommend one, provide loudness-matched A/B auditioning, accept natural-language revisions, and export release-ready audio.

The first production target is a Windows standalone application. VST3, web, macOS, and Linux are planned after the standalone mastering engine is validated.

## Core principles

- Automatic by default.
- Instrument-aware rather than only frequency-band-aware.
- Time-aware instrument/event detection.
- Honest confidence and graceful fallback to instrument families/unknown labels.
- Dedicated kick/808 intelligence.
- Preserve intentional grit, clipping, saturation, and sample character.
- Use stem separation only when it improves the result.
- Prefer source-guided processing on the original mix when full stem reconstruction would introduce artifacts.
- Generate two genuinely different masters, with one Recommended and one preservation-biased Alternative.
- Loudness-match Original/A/B during auditioning.
- Support optional references and reusable personal sound profiles.
- Allow natural-language mastering revisions.
- Optimize translation across phones, earbuds, cars, Bluetooth speakers, mono, monitors, and large systems.
- Maintain a lossless/high-precision internal pipeline.
- Local-first architecture with CPU/GPU/cloud provider fallback.

## Authoritative specification

See [`AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`](./AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md).

That document defines the product requirements, proposed architecture, instrument taxonomy, DSP/ML strategy, licensing rules, testing requirements, repository layout, implementation phases, engineering backlog, and acceptance criteria.

## Current status

**Specification / repository foundation.**

The next implementation milestone is **Phase 0 — Repository, legal, and architecture foundation**, followed by standards-compliant audio I/O/metering and the deterministic mastering baseline.