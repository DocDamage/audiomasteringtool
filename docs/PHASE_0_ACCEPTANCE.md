# Phase 0 Acceptance Checklist

- [x] Canonical repository established on `main`
- [x] Portable C++20 core library scaffold
- [x] Windows-first desktop executable scaffold
- [x] ML worker-process executable and isolation boundary
- [x] Inference provider abstraction independent of ONNX Runtime
- [x] Instrument-aware analysis data contracts
- [x] CMake presets for Windows and portable CI smoke builds
- [x] Automated build/test workflow
- [x] Core smoke/unit tests
- [x] Worker health command
- [x] Architecture document
- [x] Architecture decision records for process boundary, source preservation, and inference portability
- [x] Versioned IPC envelope specification
- [x] Model registry and model-license policy
- [x] Automated model-registry schema validation
- [x] Third-party notice process
- [x] Security baseline
- [x] Repository contribution policy

## Exit criteria

Phase 0 is complete when the Windows and portable smoke builds compile, tests pass, the worker health command succeeds, and model-policy validation passes in CI.

The following are **not Phase 0 exit criteria** and belong to later milestones:

- production ONNX Runtime package/provider integration and real production-model inference
- JUCE/VST3 integration
- production audio decoding/playback
- mastering DSP
- source separation/model weights

Phase 0 deliberately establishes replaceable boundaries before those dependencies are locked in.
