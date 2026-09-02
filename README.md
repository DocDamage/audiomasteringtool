# AudioMasteringTool

AudioMasteringTool is a Windows-first desktop and command-line application for
analyzing stereo mixes and producing two deterministic mastered candidates.

> Drop a track in. Get two loudness-matched mastering choices.

## Release status

The repository is in **Windows 1.0 release-candidate hardening**. It is not yet
approved for a public `v1.0.0` release.

The portable automated suite passes locally. Windows CI additionally exercises
the MSVC builds, worker IPC, desktop self-test, and native Media Foundation
MP3/AAC round trips. Human listening acceptance, real-model Windows acceptance,
clean-machine installer validation, and code signing remain release gates.

The authoritative live checklist is
[`docs/WINDOWS_1_0_RELEASE_CHECKLIST.md`](docs/WINDOWS_1_0_RELEASE_CHECKLIST.md).
The broader product specification is
[`AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`](AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md).

## Currently integrated product path

- Streaming WAV, AIFF, and FLAC through a pinned libsndfile runtime.
- Windows-native MP3 and AAC/M4A decode/encode through Media Foundation,
  guarded by a Windows integration test.
- BS.1770-family loudness, true-peak, waveform, spectrum, stereo, structural,
  perceptual, and integrity analysis.
- Two deterministic mastering candidates with different preservation targets.
- Loudness-matched Original/Master A/Master B auditioning.
- Local project history, recent projects, export recipes, and automatic reopen.
- Natural-language bounded revision controls in the desktop application.
- Playback translation scoring and album batch controls in the desktop app.
- Trusted, hash-pinned on-demand HTDemucs model acquisition and isolated worker
  inference, with safe stereo fallback.
- Per-monitor DPI metadata, keyboard navigation/shortcuts, privacy-gated local
  crash logs, and an Inno Setup installer.

## Deliberately gated capabilities

- Automatic source-guided Mode 1 audio changes remain disabled until the
  declared blind-listening and damage-rate policy passes.
- Mode 2 stem reconstruction is not part of the normal desktop mastering path.
- Exact 28-class instrument claims are not emitted because no production-
  approved calibrated instrument detector is configured.
- OGG/Vorbis and Opus are represented in codec contracts but are not advertised
  as standard Windows 1.0 formats.
- Interaction repair, reference-profile influence, and learned preferences have
  tested domain modules but are not claimed as fully integrated release flows.

## Build and test

Requirements: Visual Studio with the C++ Desktop workload, CMake 3.24 or newer,
Python 3.12, and a 64-bit Windows host.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug
cmake --build --preset windows-release
ctest --preset windows-debug --timeout 180 --output-on-failure
ctest --preset windows-release --timeout 180 --output-on-failure
```

Portable development validation is available on systems with Ninja and
libsndfile:

```bash
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug --timeout 180 --output-on-failure
```

## Package Windows artifacts

The packager refuses a dirty worktree by default, requires `sndfile.dll` and
`onnxruntime.dll`, stages the packaged model registry beside the executable,
writes release provenance and SHA-256 checksums, creates the portable ZIP, and
builds the installer.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1
```

For an unsigned internal smoke package only:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 `
  -AllowDirty -SkipInstaller
```

Production packaging should pass `-RequireSigning`, `-SignToolPath`, and
`-SigningCertificateThumbprint`.

## Command-line interface

```text
amt_cli --version
amt_cli codec-status
amt_cli probe input.wav
amt_cli analyze input.wav
amt_cli deep-analyze input.wav --json
amt_cli instruments input.wav --json
amt_cli plan input.wav
amt_cli master input.wav output-directory
amt_cli calibrate-source-guidance input.wav output-directory \
  --registry models/registry.json --worker amt_worker.exe \
  --model-root model-store
amt_cli audition original.wav master-a.wav master-b.wav
amt_cli export input.wav output.wav --sample-rate 44100 --bits 24
amt_cli compare input.wav output.wav --tolerance 1e-7
amt_cli play input.wav
amt_cli rerender input.wav output.wav
amt_cli verify input.wav output.wav
```

## Safety principles

- The original stereo source is canonical and is never destructively edited.
- Exact source or instrument claims require supporting evidence and confidence.
- Optional model or worker failures fall back to deterministic stereo mastering.
- Sound-changing features stay gated until their listening policy passes.
- CPU execution remains supported; CUDA is optional.
- User projects, settings, downloaded models, and caches are not deleted by the
  application uninstaller.

## Privacy

Audio, projects, analysis, models, settings, and logs remain on the local
machine. The Windows 1.0 application implements no telemetry upload transport;
the persisted telemetry preference defaults to disabled. Crash logging also
defaults to disabled. If the user opts in, the application writes a sanitized
text log locally below
`%LOCALAPPDATA%\AudioMasteringTool\crashes`; it does not upload that log.

Post-1.0 platform and plugin work begins only after the Windows acceptance gate
is complete.
