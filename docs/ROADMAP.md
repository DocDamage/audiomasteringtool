# AudioMasteringTool Roadmap

The authoritative product specification remains:

`AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`

The detailed operational plan for completing the repository is:

`docs/PROJECT_COMPLETION_IMPLEMENTATION_PLAN.md`

## Current status

### Phase 0 — Foundation — COMPLETE

- C++20 core/domain contracts
- Windows shell + CLI
- streaming audio foundation
- worker isolation / IPC
- dependency and model governance
- build/test/benchmark infrastructure

### Phase 1 — Production audio foundation + metering — COMPLETE

- streaming decode/encode abstraction
- WAV/AIFF/FLAC baseline
- resampling
- waveform cache
- playback transport
- BS.1770 loudness / true-peak metering
- spectral/stereo/integrity analysis
- transparent export path

### Phase 2 — Deterministic mastering baseline — COMPLETE

- mastering DSP nodes
- deterministic processing graph
- offline renderer
- heuristic mastering planner
- exactly two mastering candidates
- preservation-biased alternative
- loudness-matched auditioning

### Phase 3 — Structural/perceptual analysis — COMPLETE

- tempo/onset analysis
- structural sections
- macro dynamics
- perceptual heuristics
- clipping/saturation characterization
- Mix Health
- plain-language findings

### Phase 4 — Product workflow + project history — COMPLETE

- Windows mastering workflow
- Original / Master A / Master B
- recommendation rationale
- synchronized auditioning
- project persistence/history
- export recipes

### Phase 5 — Source separation + source-guided mastering — IMPLEMENTATION COMPLETE; WINDOWS RUNTIME ACCEPTANCE PENDING

Implemented:

- source separation abstraction
- trusted HTDemucs ONNX provider
- worker-isolated inference
- model installation/verification
- source fingerprint/cache identity
- evidence-first source diagnostics
- conservative source-guided stereo processing
- reconstruction safety infrastructure
- desktop diagnostics/provenance
- calibration rendering
- loudness-matched blind A/B tooling
- policy-controlled Mode 1 approval

Still required before Phase 5 is accepted:

- real `windows-msvc` build/test
- real trusted-model cold start
- real four-stem HTDemucs inference
- cache/fallback validation
- desktop sidecar reopen validation

Safety gates remain intentional:

- automatic Mode 1 remains disabled until real listening evidence passes policy
- Mode 2 reconstruction remains disabled in the normal desktop path
- CPU fallback remains required

See `docs/PHASE_5_ACCEPTANCE.md` for the exact acceptance checklist.

## Next — Phase 6: Hierarchical Instrument Intelligence

After Phase 5 runtime acceptance:

- dedicated instrument domain
- versioned hierarchical taxonomy
- time-local `InstrumentEvent` contract
- replaceable multilabel instrument detector
- confidence calibration
- explicit unknown/fallback behavior
- beat-first exact instrument vocabulary
- optional Phase 5 stem-assisted evidence
- instrument characteristics
- analysis/CLI/UI integration

Priority classes include:

- kick, snare, clap/rim, hi-hats, cymbals
- 808/sub-bass, synth bass, electric bass, upright bass
- acoustic/upright piano, Rhodes, Wurlitzer, organ
- acoustic/clean/distorted electric guitar
- strings
- brass
- saxophone/flute/woodwinds
- synth lead/pad/pluck
- lead/backing/ad-lib vocals
- percussion families

Exact instrument claims must fall back to broader family/source labels when confidence is insufficient.

## Remaining Windows 1.0 sequence

1. Phase 5 runtime acceptance + desktop application-shell consolidation
2. Phase 6 — instrument intelligence
3. Phase 7 — kick/808 + interaction-aware repair
4. Phase 8 — advanced repair/restoration
5. Phase 9 — mastering decision engine v2
6. Phase 10 — reference mastering + My Sound profiles
7. Phase 11 — natural-language revisions
8. Phase 12 — translation engine
9. Phase 13 — preference learning
10. Phase 14 — album / batch mastering
11. Phase 15 — Windows release hardening and `1.0.0`

## Post-1.0

- Phase 16 — VST3 / ARA-capable architecture (`1.1`)
- Phase 17 — web application (`1.2`)
- Phase 18 — macOS (`1.3`) and Linux (`1.4`)

## Repository policy

`main` is the complete authoritative repository.

Use short-lived feature branches only when useful for isolated development/review, merge them promptly, and delete them after merge. Do not recreate long-lived competing phase branches.

## Definition of done

A feature is not complete merely because it produces output. It requires all applicable implementation, unit/integration/audio tests, failure and cancellation behavior, versioning, diagnostics, performance validation, model/dependency provenance, documentation, user-facing integration, and listening validation when it changes sound.
