# AudioMasteringTool Roadmap

The authoritative product specification is:

`AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`

The completed operational implementation plan is:

`docs/PROJECT_COMPLETION_IMPLEMENTATION_PLAN.md`

---

## 1. Status of Windows 1.0 Release Milestones (100% Complete)

| Phase | Description | Status | Validation Suite |
|---|---|---|---|
| **Phase 0** | Core architecture, C++20 contracts, worker isolation IPC, build presets | **COMPLETE** | `phase0_core` |
| **Phase 1** | Planar audio buffers, streaming codec IO, BS.1770-5 LUFS/True-Peak metering | **COMPLETE** | `phase1_audio_analysis` |
| **Phase 2** | Mastering DSP suite (EQ, Comp, Limiter), offline renderer, Master A/B graph | **COMPLETE** | `phase2_deterministic_mastering` |
| **Phase 3** | Structural analysis, macro-dynamics, mix health heuristics | **COMPLETE** | `phase3_structural_perceptual_analysis`, `phase3_analysis_aware_planner` |
| **Phase 4** | Win32 desktop workflow, project history store, export recipes | **COMPLETE** | `phase4_project_history_and_export_recipes` |
| **Phase 5** | Source-guided stem separation, HTDemucs inference, reconstruction safety | **COMPLETE** | 14 dedicated test suites (`phase5_*`) |
| **Phase 6** | Hierarchical 28-class instrument taxonomy, confidence calibration | **COMPLETE** | `phase6_instrument_taxonomy_and_inference`, `phase6_instrument_characteristics` |
| **Phase 7** | Pairwise interaction analysis, low-end kick/808 collision repair, damage guard | **COMPLETE** | `phase7_interaction_evidence_and_damage_guard`, `phase7_interaction_engine_and_trackers` |
| **Phase 8** | Audio restoration tools (de-clip, de-click, hum filter, phase restoration) | **COMPLETE** | `phase8_restoration_and_repair` |
| **Phase 9** | Automated Decision Engine v2, spectral targets, Master A/B generation | **COMPLETE** | `phase9_decision_engine_v2` |
| **Phase 10** | Reference track matching & "MySound" personal profile extraction | **COMPLETE** | `phase10_reference_mastering_and_my_sound` |
| **Phase 11** | Natural-language revision engine (intent parser, constraint validator, plan editor) | **COMPLETE** | `phase11_natural_language_revision` |
| **Phase 12** | Playback translation simulation (9 acoustic environments, survival scoring) | **COMPLETE** | `phase12_playback_translation_simulation` |
| **Phase 13** | Preference learning (continuous bias vector, recency weighting, JSONL store) | **COMPLETE** | `phase13_preference_learning` |
| **Phase 14** | Album / batch mastering (collection analysis, dynamic cohesion, queue engine) | **COMPLETE** | `phase14_album_batch_mastering` |
| **Phase 15** | Windows standalone hardening, cache budget eviction, GUI polish, Inno Setup | **COMPLETE** | `phase15_windows_standalone_hardening` |

**Test Result:** All 35 CTest test suites are active and passing at 100% on MSVC C++20 Release.

---

## 2. Post-1.0 Roadmap

### Phase 16 — VST3 / CLAP / ARA Plugin Architecture (`v1.1`)
- Expose the headless mastering decision engine and real-time DSP as a VST3 / CLAP plugin.
- Support ARA2 (Audio Random Access) for DAW integration (direct track timeline transfer).
- Implement low-latency preview mode for live mix-bus auditioning.

### Phase 17 — Web Application (`v1.2`)
- Compile audio processing core to WebAssembly (Wasm / SIMD).
- Web Audio API transport with loudness-matched comparison.
- WebGPU / ONNX Runtime Web for browser-based instrument classification.

### Phase 18 — Multi-Platform Native Releases (`v1.3` / `v1.4`)
- **macOS (`v1.3`)**: CoreAudio native playback backend, Metal / CoreML inference acceleration, Apple Silicon universal binary (`arm64` + `x86_64`), `.dmg` installer.
- **Linux (`v1.4`)**: PipeWire / ALSA native playback, AppImage and Flatpak distribution.
