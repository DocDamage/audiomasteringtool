# Phase 6 production data and model decision

## Chosen path

Train and export an AudioMasteringTool-owned multilabel instrument detector. Do not ship an opaque third-party checkpoint.

Initial data sources, each recorded with exact release/version/checksum in the frozen corpus manifest:

- **OpenMIC-2018** (CC BY 4.0): polyphonic, multi-instrument 10-second clips; primary real-music identity/unknown-rejection evaluation.
- **Slakh2100** (CC BY 4.0): aligned MIDI, stems, and mixtures; primary event/onset, kick/bass, and interaction supervision.
- **NSynth** (CC BY 4.0): isolated-note family/timbre augmentation only; it must not be used as evidence that a label works in a full mix.

Explicitly excluded from production training/evaluation unless a separate written license is added:

- MTG-Jamendo: repository metadata is CC BY-NC-SA and its documentation requires authorization for commercial use.
- MUSDB18: academic-access/licensing constraints and mixed non-commercial sources.

## v1 vocabulary

Production v1 is deliberately narrower than the long-term taxonomy: kick, snare, clap, closed/open hi-hat, 808/sub-bass, synth bass, electric bass, acoustic piano, electric piano, organ, acoustic/electric guitar, synth lead/pad, lead/backing vocal, strings, brass, plus broad source/family fallbacks.

All labels retain `research` support until the model completes the gates below. The shipping detector may emit only labels with per-class calibrated thresholds and a retained unknown class.

## Required artifacts before promotion

1. `frozen-corpus-v1.jsonl`: source URL, license, attribution, split, audio hash, artist/track leakage group, label provenance.
2. Reproducible environment lock and training configuration.
3. Held-out metrics: per-class precision/recall/F1, family and exact-label accuracy, unknown rejection, event onset/offset, calibration error.
4. Negative/unknown test set frozen before training selection.
5. ONNX export with SHA-256, input/output contract, deterministic CPU regression fixture.
6. Legal, security, and commercial-redistribution review records.
7. Blind listening evaluation for Phase 7 repair decisions, with damage limits declared before review.

No model is production eligible merely because its aggregate accuracy is high.
