# AudioMasteringTool 1.0 Release Audit

**Current verdict: not yet approved for public Windows 1.0 release.**

The earlier audit was superseded by the release-hardening work in the current
tree. Version metadata, packaging layout, dependency staging, native lossy codec
support, DPI metadata, keyboard access, and crash-log wiring have been addressed.
Automatic Mode 1 remains safely disabled.

## Verified in the current development environment

- CMake project and runtime version are `1.0.0`.
- The portable Ninja build succeeds.
- The complete portable CTest suite passes.
- Dependency and model registries validate.
- Installer input now comes from the same self-contained staged tree as the
  portable release.
- Release packaging requires `sndfile.dll` and `onnxruntime.dll` and generates
  provenance, a portable ZIP, and SHA-256 checksums.
- Local MSVC Debug and Release builds succeed with all 36 tests passing.
- Native Windows Media Foundation MP3 and AAC/M4A round trips pass.
- The staged worker IPC and deterministic desktop pipeline self-tests pass.
- A dirty-tree portable packaging rehearsal completes and its full checksum
  manifest verifies.

## Evidence still required

- Clean MSVC Debug and Release results and CI checks for the final commit.
- Real HTDemucs cold-start, inference, cache, cancellation, and fallback matrix.
- Predeclared listening/damage-policy evidence before Mode 1 can be enabled.
- A production-approved instrument detector and calibration evidence before
  exact 28-class instrument claims can be enabled.
- Clean Windows VM installer/uninstaller and ordinary-user end-to-end test.
- Signed binaries and installer with published checksums.
- Frozen release-corpus performance, audio-regression, and listening results.

See [`docs/WINDOWS_1_0_RELEASE_CHECKLIST.md`](docs/WINDOWS_1_0_RELEASE_CHECKLIST.md)
for owners, commands, and required evidence.
