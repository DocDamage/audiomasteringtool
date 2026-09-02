# Architecture

## 1. System Overview

AudioMasteringTool is designed around clean domain isolation, high-performance C++20 planar DSP processing, out-of-process machine learning workers, and modular host frontends (Desktop Win32 standalone, CLI, and future VST3/CLAP/Web).

```
┌─────────────────────────────────────────────────────────────┐
│                       Host Layer                            │
│  ┌────────────────────────┐  ┌───────────────────────────┐  │
│  │ audiomasteringtool.exe │  │        amt_cli.exe        │  │
│  │   (Win32 Desktop GUI)  │  │ (Command Line Controller) │  │
│  └───────────┬────────────┘  └─────────────┬─────────────┘  │
└──────────────┼─────────────────────────────┼────────────────┘
               ▼                             ▼
┌─────────────────────────────────────────────────────────────┐
│                    Mastering Core Domain                    │
│  ┌───────────────────────┐   ┌───────────────────────────┐  │
│  │     amt_mastering     │   │       amt_decision        │  │
│  │   (Offline Renderer   │   │  (Decision Engine v2,     │  │
│  │    & Master A/B Graph)│   │   Tonal Balance, Targets) │  │
│  └───────────┬───────────┘   └─────────────┬─────────────┘  │
│              ▼                             ▼                │
│  ┌───────────────────────┐   ┌───────────────────────────┐  │
│  │      amt_repair       │   │     amt_interactions      │  │
│  │  (Restoration Tools,  │   │  (Pairwise Low-End,       │  │
│  │   De-click, Hum Filt) │   │   Kick/808 Collision Mgmt)│  │
│  └───────────┬───────────┘   └─────────────┬─────────────┘  │
│              ▼                             ▼                │
│  ┌───────────────────────┐   ┌───────────────────────────┐  │
│  │      amt_revision     │   │      amt_translation      │  │
│  │   (Natural Language   │   │  (9 Playback Environment  │  │
│  │    Revision Engine)   │   │   Acoustic Simulations)   │  │
│  └───────────┬───────────┘   └─────────────┬─────────────┘  │
│              ▼                             ▼                │
│  ┌───────────────────────┐   ┌───────────────────────────┐  │
│  │     amt_reference     │   │      amt_preferences      │  │
│  │  (Reference Match &   │   │  (User Preference Learning│  │
│  │   MySound Profiles)   │   │   & Continuous Bias Store)│  │
│  └───────────┬───────────┘   └─────────────┬─────────────┘  │
│              ▼                             ▼                │
│  ┌───────────────────────┐   ┌───────────────────────────┐  │
│  │       amt_batch       │   │       amt_settings        │  │
│  │   (Album Cohesion &   │   │  (Config, Cache Budget,   │  │
│  │    Batch Queue Engine)│   │   Model Registry Manager) │  │
│  └───────────┬───────────┘   └─────────────┬─────────────┘  │
└──────────────┼─────────────────────────────┼────────────────┘
               ▼                             ▼
┌─────────────────────────────────────────────────────────────┐
│                 Analysis & DSP Foundations                  │
│  ┌───────────────────────┐   ┌───────────────────────────┐  │
│  │       amt_dsp         │   │       amt_analysis        │  │
│  │ (EQ, Comp, MultiBand, │   │ (BS.1770 LUFS, True Peak, │  │
│  │  Limiter, Saturation) │   │  Spectrum, Stereo Width)  │  │
│  └───────────┬───────────┘   └─────────────┬─────────────┘  │
│              ▼                             ▼                │
│  ┌───────────────────────┐   ┌───────────────────────────┐  │
│  │      amt_codec        │   │        amt_audio          │  │
│  │ (Dynamic libsndfile,  │   │ (Planar Float32 Buffers,  │  │
│  │  WAV, AIFF, FLAC IO)  │   │  Sinc Resampler, Waveform)│  │
│  └───────────┬───────────┘   └─────────────┬─────────────┘  │
└──────────────┼─────────────────────────────┼────────────────┘
               ▼                             ▼
┌─────────────────────────────────────────────────────────────┐
│                 Worker & ML Isolation Layer                 │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                      amt_worker                       │  │
│  │    (Isolated Out-of-Process Inference Host, HTDemucs  │  │
│  │     Stem Separation, ResNet Instrument Classifier)    │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Core Modules & Target Mapping

| Target | Library / Namespace | Description |
|---|---|---|
| `amt_core` | `amt::core` | Common definitions, error types, cancellation tokens, versioning. |
| `amt_audio` | `amt::audio` | Planar float32 `AudioBuffer`, windowed-sinc `Resampler`, multiresolution `WaveformCache`. |
| `amt_codec` | `amt::codec` | Dynamic libsndfile loader, WAV/AIFF/FLAC streaming encoders/decoders with progress. |
| `amt_analysis` | `amt::analysis` | BS.1770-5 Annex 1 / EBU R128 loudness, true-peak, spectral centroid, stereo correlation. |
| `amt_dsp` | `amt::dsp` | EQ (linear/minimum phase), dynamic EQ, compressors, multiband dynamics, saturation, limiter. |
| `amt_separation` | `amt::separation` | HTDemucs stem separation contracts, fingerprint cache, and reconstruction contracts. |
| `amt_instruments` | `amt::instruments` | 28-class hierarchical instrument taxonomy, confidence calibration, event timelines. |
| `amt_interactions` | `amt::interactions` | Pairwise interaction analysis, kick/808 low-end collision detection, damage guards. |
| `amt_repair` | `amt::repair` | De-clipping, de-clicking, resonance hum notch filtering, stereo phase restoration. |
| `amt_decision` | `amt::decision` | Automated decision engine v2, macro-dynamics assessment, spectral target resolution. |
| `amt_mastering` | `amt::mastering` | Mastering plan builder, Master A / Master B graph executor, offline renderer. |
| `amt_playback` | `amt::playback` | Native WASAPI audio output engine, loudness-matched comparison transport. |
| `amt_reference` | `amt::reference` | Reference track spectrum matching and "MySound" personal sound profile curves. |
| `amt_revision` | `amt::revision` | Natural-language intent parser, constraint validator, and plan revision applicator. |
| `amt_translation` | `amt::translation` | 9 playback environment acoustic simulation models and survival scoring. |
| `amt_preferences` | `amt::preferences` | User preference continuous bias store with recency weighting and JSONL export. |
| `amt_batch` | `amt::batch` | Album collection analysis, proportional dynamic cohesion planner, batch queue. |
| `amt_settings` | `amt::settings` | Application settings store, cache eviction manager, model verification manager. |
| `amt_worker` | `amt::worker` | Out-of-process worker executable for crash-isolated ML inference. |
| `amt_cli` | `amt::cli` | Command-line interface for headless automation, analysis, and batch processing. |
| `audiomasteringtool` | `amt::app` | Complete Win32 desktop application shell with interactive GUI controls. |

---

## 3. Audio & Safety Invariants

1. **Original Signal is Canonical**: The original stereo source mix is always preserved. Blind stem separation is never substituted unless safety-gated and verified.
2. **Deterministic Processing**: All DSP chains produce bit-exact deterministic results for identical input and plan configurations.
3. **Lossless Internal Pipeline**: All internal processing runs in 32-bit floating point planar format. Dithering (TPDF) is strictly applied only when quantizing down to integer bit depths.
4. **Crash Isolation**: High-risk ML operations run out-of-process in `amt_worker.exe` with framed IPC.
