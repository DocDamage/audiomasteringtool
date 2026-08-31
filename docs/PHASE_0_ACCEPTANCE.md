# Phase 0 acceptance

Authoritative source: `AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`, Phase 0.

## Deliverables

- [x] CMake project skeleton
- [x] portable `amt_core` static library
- [x] Windows CLI proof of concept (`amt_cli`)
- [x] desktop shell skeleton
- [x] CTest-based unit/smoke test harness
- [x] GitHub Actions Windows workflow
- [x] coding conventions
- [x] architecture decision records
- [x] machine-readable third-party dependency manifest
- [x] model-license manifest/registry format
- [x] benchmark harness (`amt_bench`)

## Mandatory technical spikes

- [x] JUCE 8 standalone shell vs alternative host feasibility
- [x] ONNX Runtime CPU + CUDA C++ inference path implemented as a reproducible spike
- [x] ONNX Runtime native WebGPU evaluation recorded
- [x] audio decoder strategy and FFmpeg licence-safe-build evaluation recorded
- [x] 60-minute streaming decode/render test implemented and executed
- [x] worker-process IPC test implemented and executed

See `docs/PHASE_0_SPIKES.md` for findings and limitations.

## Exit criteria

- [x] repository is dependency-light and designed to clean-clone/configure/build on Windows through the checked-in MSVC preset
- [x] unit tests are wired into Windows CI
- [x] CLI streams and losslessly re-renders baseline integer-PCM WAV files
- [x] CLI streams and losslessly re-renders baseline integer-PCM FLAC files
- [x] selected dependency strategy is recorded in `docs/DEPENDENCY_STRATEGY.md`

## Validation evidence completed during Phase 0 implementation

Local portable validation on the exact implementation included:

- C++20 configure/build with Ninja/GCC
- CTest core suite passing
- worker `--health` response
- real worker-process stdio IPC health/error/shutdown exchange
- dynamically loaded libsndfile runtime detection
- 48 kHz stereo PCM16 WAV re-render with decoded-sample equality
- 48 kHz stereo PCM16 FLAC re-render with decoded-sample equality
- 60-minute, 8 kHz mono PCM16 streaming WAV re-render with decoded-sample equality
- benchmark harness execution
- dependency manifest validator
- model registry validator

Windows CI is the authoritative MSVC clean-clone gate. The ONNX CPU spike is automated on GitHub-hosted Windows. The CUDA runtime spike requires an NVIDIA-equipped self-hosted Windows runner; the repository includes that opt-in job rather than pretending a non-GPU hosted runner verified CUDA execution.

## Deliberately outside Phase 0

- production codec/export abstraction
- playback engine/waveform UI
- BS.1770-5 implementation and conformance vectors
- production ONNX model selection/weights
- production JUCE integration
- mastering DSP
- source separation
- instrument-classification models

Those belong to later phases and must build on the boundaries established here.
