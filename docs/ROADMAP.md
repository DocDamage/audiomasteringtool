# AudioMasteringTool Roadmap

The authoritative product requirements are in
`AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`. Release acceptance is
tracked in `docs/WINDOWS_1_0_RELEASE_CHECKLIST.md`.

## Windows 1.0 release-candidate status

| Area | Current status |
|---|---|
| Core, audio buffers, metering, analysis | Integrated; automated coverage active |
| Deterministic DSP and two-master rendering | Integrated; listening benchmark pending |
| Project history, audition, desktop workflow | Integrated; clean-machine acceptance pending |
| Trusted HTDemucs worker and diagnostics | Implemented and safety-gated; deterministic Windows runtime matrix passed, representative-track listening pending |
| Automatic source-guided Mode 1 changes | Disabled pending declared listening/damage policy |
| Mode 2 reconstruction | Excluded from the normal 1.0 desktop path |
| 28-class instrument taxonomy | Contracts and tests exist; no production-approved detector |
| Interaction and restoration modules | Domain tests exist; full release-flow integration/listening evidence pending |
| Decision v2, reference, preference modules | Domain tests exist; unsupported claims remain gated |
| Natural-language revision | Integrated into desktop flow |
| Translation scoring and album batch | Integrated into desktop controls; end-to-end acceptance pending |
| Windows MP3/AAC | Media Foundation implementation and integration test added; final Windows result pending |
| Installer and portable package | Hardened staging implemented; clean-VM and signing acceptance pending |

Windows `v1.0.0` may be tagged only after every applicable item in the live
release checklist has evidence. Version labels do not substitute for acceptance.

## Post-1.0 roadmap

### Phase 16 — VST3 / CLAP / ARA-capable architecture (`v1.1`)

- Expose the accepted headless mastering contracts through plugin-safe wrappers.
- Add realtime-safety, state recall, and host compatibility validation.
- Keep offline-only analysis and model work outside the audio callback.

### Phase 17 — Web application (`v1.2`)

- Reuse versioned mastering contracts through WebAssembly or a controlled service.
- Add Web Audio transport and capability-based local/cloud execution.
- Preserve source privacy, cancellation, and project provenance.

### Phase 18 — native platform releases (`v1.3` / `v1.4`)

- macOS: CoreAudio, Apple Silicon, signing/notarization, and a `.dmg` installer.
- Linux: PipeWire/ALSA, AppImage/Flatpak, and distribution-specific codec review.

Post-1.0 work begins only after the Windows standalone release gate is complete.
