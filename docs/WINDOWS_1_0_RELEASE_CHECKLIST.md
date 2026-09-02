# Windows 1.0 Release Checklist

This file is the live release gate. A source implementation or passing unit test
does not by itself complete a runtime, listening, packaging, or signing item.

## Source and automated validation

- [x] CMake and runtime versions are `1.0.0`.
- [x] Portable Debug build and CTest suite pass.
- [x] Model and dependency manifests validate.
- [x] Automatic Mode 1 is disabled in the production registry.
- [x] Windows MP3/AAC integration coverage exists.
- [ ] Final source tree is committed and `git status --porcelain` is empty.
- [ ] Windows MSVC Debug build and CTest pass for the final commit.
- [ ] Windows MSVC Release build and CTest pass for the final commit.
- [ ] Required GitHub Actions checks pass for the final commit.

Commands:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug
cmake --build --preset windows-release
ctest --preset windows-debug --timeout 180 --output-on-failure
ctest --preset windows-release --timeout 180 --output-on-failure
python scripts/validate_dependencies.py
python scripts/validate_model_registry.py
```

Record the commit SHA and CI run links in the release record.

## Real model and worker acceptance

- [ ] Delete the per-user model and complete a cold, pinned download.
- [ ] Confirm exact byte count and SHA-256 before atomic publication.
- [ ] Load the packaged ONNX Runtime DLLs from the staged release.
- [ ] Run a real track through the packaged worker on CPU.
- [ ] Verify finite, correctly shaped drums/bass/other/vocals output.
- [ ] Verify identical second request is a cache hit.
- [ ] Verify source, model, topology, and corrupt-metadata cache invalidation.
- [ ] Verify missing internet, interrupted download, cancellation, worker crash,
      timeout, corrupt model, and insufficient-disk behavior.
- [ ] Verify all optional failures preserve the source and retain stereo mastering.
- [ ] Record peak RAM, processing ratio, source/model hashes, and Windows version.

Store sanitized evidence below `release-evidence/v1.0.0/model-runtime/`. Do not
commit copyrighted test audio or the downloaded model artifact.

## Listening and sound-changing policy

- [ ] Freeze a representative release corpus manifest.
- [ ] Declare benefit, damage, already-good-mix, and minimum-response thresholds
      before decoding blind responses.
- [ ] Generate attenuation-only loudness-matched blind bundles.
- [ ] Complete independent responses and lock them before decoding.
- [ ] Confirm the recommended stereo mastering candidate clears its benchmark.
- [ ] Confirm every automatically enabled repair clears its damage gate.
- [ ] Confirm source-guided Mode 1 clears its policy before changing
      `automaticMode1Approved` to `true`.

Until this section passes, Mode 1 must remain false and exact instrument claims
must remain disabled unless a separately calibrated production detector exists.

## Product integration acceptance

- [ ] Normal user can open, analyze, master, audition, revise, and export.
- [ ] Original/Master A/Master B comparison remains synchronized and
      loudness-matched.
- [ ] Project history reopens after restart and after optional cache deletion.
- [ ] Recent projects, batch flow, translation selection, and settings work.
- [ ] MP3 and AAC/M4A import/export work on a clean supported Windows install.
- [ ] Unicode paths, long paths, mono, unusual sample rates, read-only sources,
      corrupt media, cancellation, and low-disk paths are exercised.
- [ ] No known source mutation, project loss, or silent export failure exists.
- [ ] Unsupported features are absent from claims or visibly marked unavailable.

## Installer and distribution

- [x] Portable and installer consume one staged release tree.
- [x] Packaged registry is installed at `{app}\models\registry.json`.
- [x] Packager requires `sndfile.dll` and `onnxruntime.dll`.
- [x] Packager writes provenance and SHA-256 checksums.
- [x] Packager creates a portable ZIP and a separate archive checksum.
- [x] A dirty-tree portable packaging rehearsal passes with the complete
      Release CTest suite and its checksum manifest verifies.
- [x] Installer leaves per-user projects/settings/models/cache untouched.
- [ ] Clean Windows VM install and first launch succeed as a non-admin user.
- [ ] Installed application completes an end-to-end master and export.
- [ ] Uninstall removes application files and preserves per-user data.
- [ ] Portable archive contents match the checksum manifest.
- [ ] Executables and installer have valid Authenticode signatures and timestamp.
- [ ] Installer and portable archive hashes are published with release notes.

Production packaging:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 `
  -RequireSigning `
  -SignToolPath "C:\path\to\signtool.exe" `
  -SigningCertificateThumbprint "CERTIFICATE_THUMBPRINT"
```

## Documentation and legal

- [x] Current product claims distinguish integrated and gated capabilities.
- [x] Model acquisition, identity, provenance, and licence are documented.
- [x] Production dependency versions are reconciled with CMake.
- [ ] Legal review confirms model commercial-use and download terms.
- [ ] LGPL distribution/relinking obligations for libsndfile are approved.
- [ ] Final package contains every required copyright and licence notice.
- [x] Privacy text describes local opt-in crash logs and no default telemetry.

## Local release-candidate validation record

On 2026-09-02, before the final commit:

- Portable Ninja Debug built successfully; all 35 tests passed.
- Windows MSVC Debug built successfully; all 36 tests passed.
- Windows MSVC Release built successfully; all 36 tests passed.
- Native Media Foundation MP3 and AAC/M4A round trips passed.
- Worker health and staged worker IPC passed.
- The staged desktop self-test persisted both mastered candidates.
- The dirty-tree portable packaging rehearsal completed and every staged
  SHA-256 entry verified.

This is local candidate evidence, not final-commit or clean-machine release
authorization. CI, installer, signing, real-model, listening, and product
acceptance gates above remain open.

## Release authorization

Only after every applicable item above passes:

- [ ] Create the immutable `v1.0.0` tag from the accepted commit.
- [ ] Build artifacts from that tag in the trusted release environment.
- [ ] Re-verify signatures and checksums after upload.
- [ ] Publish release notes with known limitations and manual update instructions.
