# AudioMasteringTool — Full Project Completion Implementation Plan

**Repository:** `DocDamage/audiomasteringtool`  
**Authoritative branch:** `main`  
**Current product milestone:** Windows `1.0.0` release-candidate hardening; not yet accepted
**Primary production target:** Windows standalone `1.0` (final MSVC/runtime/listening/installer evidence pending)
**Post-1.0 targets:** VST3/ARA-capable architecture, web, macOS, Linux  

---

# 1. Purpose

This document is the operational plan for finishing AudioMasteringTool from the current Phase 5 codebase through the complete product roadmap.

It is intentionally more execution-oriented than the original product specification. The authoritative product requirements remain in:

- `AUDIOMASTERINGTOOL_SPEC_AND_IMPLEMENTATION_PLAN.md`
- `docs/PHASE_5_ACCEPTANCE.md`

This plan answers:

- what remains
- what order it should be built in
- where new code should live
- which contracts must be stabilized first
- what must be tested
- what may remain gated
- what qualifies each phase as complete
- what qualifies Windows `1.0` as complete
- how post-1.0 VST3/web/macOS/Linux work should proceed without rewriting the core

The end goal remains:

> Drop a track in. Get a finished master.

The product must remain an autonomous audio-finishing tool, not become a DAW.

---

# 2. Current Repository Truth

## 2.1 Completed foundation

Phases 0–4 are already represented in `main`:

- C++20 core architecture
- streaming audio I/O
- WAV/AIFF/FLAC baseline
- BS.1770 loudness / true-peak analysis
- waveform and transport
- structural/perceptual analysis
- deterministic mastering DSP
- two-candidate mastering
- loudness-matched Original/A/B auditioning
- project/history/export workflow
- Windows desktop shell
- CLI
- worker-process architecture
- model/dependency governance

## 2.2 Phase 5 implementation state

Phase 5 source-guided mastering is implemented as a safe gated system.

Implemented:

- source separation abstraction
- source fingerprints and cache identity
- trusted HTDemucs ONNX artifact installation
- worker-isolated ONNX inference
- diagnostic source separation
- source-specific issue inference
- conservative Mode 1 source-guided stereo processing
- reconstruction artifact-risk infrastructure
- desktop diagnostic provenance
- calibration rendering
- loudness-matched blind A/B tooling
- blind response decoding
- policy-controlled Mode 1 approval
- Phase 5 automated C++ and Python test coverage

Still requiring real Windows runtime acceptance:

- native `windows-msvc` configure/build
- complete CTest run on Windows
- first-run model acquisition under `%LOCALAPPDATA%`
- real ONNX Runtime DLL loading
- real HTDemucs four-stem inference
- real cache hit/invalidation verification
- forced failure/fallback verification
- real desktop sidecar persistence/reopen verification
- real calibration candidate generation on audio

## 2.3 Safety gates that must remain intact

Do not bypass these to manufacture completion:

- `automaticMode1Approved` remains `false` until a real listening policy passes.
- Mode 2 reconstruction remains disabled in the ordinary desktop mastering path.
- CPU remains a supported execution path.
- CUDA remains optional.
- generic stereo analysis may not claim an exact source caused a problem.
- exact instrument claims require Phase 6 evidence.

## 2.4 Current architecture areas

Existing major modules under `src/`:

- `analysis`
- `app`
- `audio`
- `bench`
- `cli`
- `codec`
- `core`
- `dsp`
- `mastering`
- `playback`
- `project`
- `separation`
- `worker`

New modules should be added only when they represent durable domain boundaries rather than temporary phase wrappers.

---

# 3. Repository and Branch Policy Going Forward

The repository was cleaned so that `main` is the complete source of truth. Preserve that simplicity.

## Rules

1. `main` must always represent the complete current repository.
2. Use short-lived feature branches only when needed for isolated work or review.
3. Merge frequently enough that a feature branch never becomes a second competing product history.
4. Delete feature branches after merge.
5. Do not create long-lived `phase6`, `phase7`, etc. branches that become alternative repositories.
6. Never put important production work only on an abandoned branch.
7. Tag meaningful accepted milestones instead of preserving dozens of stale branches.

Recommended milestone tags:

- `v0.5-phase5-accepted`
- `v0.6-instrument-intelligence`
- `v0.7-interaction-repair`
- `v0.8-intelligent-mastering`
- `v0.9-release-candidate`
- `v1.0.0`

---

# 4. Required Execution Order

The remaining work should proceed in this dependency order:

1. **Phase 5 acceptance + application-shell consolidation**
2. **Phase 6 — Hierarchical instrument intelligence**
3. **Phase 7 — Kick/808 and interaction-aware repair**
4. **Phase 8 — Advanced repair/restoration**
5. **Phase 9 — Mastering decision engine v2**
6. **Phase 10 — Reference mastering + My Sound profiles**
7. **Phase 11 — Natural-language revision engine**
8. **Phase 12 — Translation engine**
9. **Phase 13 — Preference learning**
10. **Phase 14 — Album / batch mastering**
11. **Phase 15 — Windows standalone hardening and 1.0 release**
12. **Phase 16 — VST3 / ARA-capable architecture**
13. **Phase 17 — Web application**
14. **Phase 18 — macOS + Linux**

Research may overlap, but production integration should respect these dependencies.

---

# 5. Phase 5 Closure — Runtime Acceptance + Architecture Consolidation

This is the immediate blocker before Phase 6.

## 5.1 Windows runtime acceptance

Run on a real Windows MSVC environment:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
python scripts/validate_model_registry.py
```

Required targets:

- `audiomasteringtool`
- `amt_cli`
- `amt_worker`
- all Phase 5 libraries
- all Phase 5 C++ tests
- Python calibration tooling tests

Fix compiler/linker/runtime problems rather than treating CI status as the product.

## 5.2 Real trusted-model validation

Delete the installed per-user model first and validate the entire cold-start flow:

1. launch a Phase 5 job
2. detect missing trusted artifact
3. download only from the trusted catalog URL
4. enforce exact byte size
5. enforce exact SHA-256
6. atomically publish the verified model
7. place it below `%LOCALAPPDATA%\AudioMasteringTool\models`
8. launch `amt_worker.exe`
9. load packaged ONNX Runtime DLLs
10. run a real source through HTDemucs
11. generate drums/bass/other/vocals stems
12. verify sample rate, channels, frames and finite samples
13. infer source-specific issues where evidence exists
14. keep production mastering on canonical stereo while Mode 1 approval remains false

## 5.3 Cache validation

Test:

- first request = inference
- identical second request = cache hit
- changed source bytes = cache miss
- changed source fingerprint = cache miss
- changed model version/hash = cache miss
- changed requested stem topology = cache miss
- corrupt cache metadata = safe miss/rebuild
- canonical source never modified

## 5.4 Failure/fallback matrix

Force independently:

- missing registry
- malformed registry
- missing worker
- worker crash
- worker timeout
- worker output overflow
- model download failure
- content-length mismatch
- hash mismatch
- corrupt installed model
- unsupported channel topology
- cancellation during model download
- cancellation during inference
- cancellation during render
- insufficient disk space where reproducible

Expected behavior:

- canonical source remains intact
- partial artifacts are cleaned
- job fails clearly or falls back safely
- stereo mastering remains available when source diagnostics are optional

## 5.5 Desktop provenance restore

Phase 5 writes:

- `source-diagnostics-v1.json`
- `source-diagnostics-v1.txt`

Add project-reopen support so the desktop UI can load and display persisted diagnostic state even when no in-memory Phase 5 report exists.

Required behavior:

- schema/version check
- tolerate missing sidecar
- tolerate unknown future fields
- reject malformed content safely
- do not rewrite old project history merely by opening it

## 5.6 Consolidate the desktop application shell

Current `Phase5Main.cpp` redirects the Phase 4 shell using macro substitution. This must not become the pattern for every future phase.

Refactor before Phase 6 UI expansion.

Recommended new boundary:

```text
src/app/
  AppMain.cpp
  DesktopApplication.h/.cpp
  MasteringSessionController.h/.cpp
  ProjectController.h/.cpp
  DesktopViewModel.h/.cpp
  Win32Host.h/.cpp
```

Goals:

- one real desktop entry point
- no `Phase6Main.cpp`, `Phase7Main.cpp`, etc.
- UI host calls stable application services
- mastering logic remains outside the UI module
- controllers expose cancellable job APIs
- project state and in-memory analysis are explicit
- future VST3/web wrappers do not depend on Win32 widgets

Keep the existing Windows UI working while extracting responsibilities incrementally.

## 5.7 Phase 5 closure deliverables

- Windows build/test evidence
- real model-startup evidence
- fallback test record
- cache test record
- sidecar restore implementation
- consolidated desktop entry point
- updated `docs/PHASE_5_ACCEPTANCE.md` with completed checks
- milestone tag after acceptance

## Phase 5 exit gate

Do not start production Phase 6 integration until the Phase 5 acceptance document can truthfully be marked passed.

---

# 6. Phase 6 — Hierarchical Instrument Intelligence v1

## Implementation status (2026-09)

The repository now contains the Phase 6 safety foundation: `src/instruments`, a validated stable taxonomy, time-local event contract, hierarchical confidence fallback/unknown behavior, detector/model-governance contracts, analysis-report serialization, CLI output, and frozen-corpus manifest validation. It does **not** yet contain a production-approved detector or its evaluation evidence, so exact instrument events remain empty by default and Phase 6 is not accepted.

Target milestone: `0.6`

This is the next major product capability.

The system must identify the most specific supported instrument when confidence permits and fall back through a hierarchy when it does not.

Example:

```text
808/sub-bass -> bass -> bass source -> unknown
Rhodes -> electric piano -> keyboards -> tonal instrument -> unknown
closed hi-hat -> hi-hat -> drums/percussion -> percussion -> unknown
```

Exact labels must never be presented with more certainty than the evidence supports.

## 6.1 Add a dedicated instrument domain

Create:

```text
src/instruments/
  CMakeLists.txt
  include/amt/instruments/
    InstrumentTaxonomy.h
    InstrumentEvent.h
    InstrumentAttributes.h
    InstrumentDetector.h
    InstrumentInference.h
    InstrumentCalibration.h
    InstrumentEventTracker.h
    InstrumentEvidence.h
  src/
```

Do not bury instrument identity inside `analysis` or `separation`. It is a durable domain with its own models, taxonomy, calibration and tests.

## 6.2 Taxonomy contract

Implement stable IDs rather than UI strings.

Each taxonomy node should define:

- stable machine ID
- display label
- parent ID
- hierarchy level
- optional aliases
- source role
- instrument family
- whether mutually exclusive with siblings
- whether it may coexist with siblings
- model support state
- minimum confidence policy

Required hierarchy levels:

- Level 0: source role
- Level 1: family
- Level 2: instrument
- Level 3: non-exclusive production attributes

## 6.3 Priority instrument vocabulary

### Low end

- 808/sub-bass
- synth bass
- electric bass guitar
- acoustic/upright bass
- Reese-style bass
- distorted bass

### Drum kit

- kick
- snare
- clap
- rimshot/rim
- closed hi-hat
- open hi-hat
- crash
- ride
- toms

### Percussion

- shaker
- tambourine
- conga
- bongo
- cowbell
- claves
- triangle
- hand percussion
- generic percussion

### Keyboards

- grand/acoustic piano
- upright piano
- Rhodes-style electric piano
- Wurlitzer-style electric piano
- clavinet
- Hammond/organ
- generic organ
- harpsichord

### Guitars / plucked sources

- acoustic guitar
- clean electric guitar
- distorted electric guitar
- muted guitar
- nylon-string guitar
- banjo
- mandolin
- harp

### Strings

- violin
- viola
- cello
- double bass
- string ensemble
- pizzicato strings

### Brass

- trumpet
- trombone
- French horn
- tuba
- brass ensemble

### Woodwinds

- saxophone
- flute
- clarinet
- oboe
- bassoon
- woodwind ensemble

### Synths

- synth lead
- synth pad
- synth pluck
- synth keys
- bell/chime synth
- arpeggiated synth
- texture/drone

### Vocals

- lead vocal
- backing vocal
- ad-lib
- choir/group vocal
- spoken vocal
- vocal chop/sample

### Production/sample elements

- vinyl/sample texture
- orchestral sample
- chopped melodic sample
- noise/foley
- riser
- impact
- reverse effect
- ambience

## 6.4 Production attributes

Attributes are non-exclusive probabilities, not replacements for identity.

Initial attributes:

- acoustic
- electric
- synthetic
- sampled likelihood
- clean
- distorted
- saturated
- dry
- reverberant
- mono
- stereo
- bright
- dark
- transient
- sustained
- short
- aggressive
- warm
- lo-fi

## 6.5 Instrument model provider abstraction

Add an interface independent of a specific ML model.

Example responsibilities:

```cpp
class IInstrumentDetector {
public:
  virtual InstrumentDetectionResult detect(
      const InstrumentDetectionRequest& request,
      std::string& error,
      const CancellationToken* cancellation,
      const ProgressCallback& progress) = 0;
  virtual InstrumentModelInfo info() const = 0;
  virtual ~IInstrumentDetector() = default;
};
```

The business logic must not depend on one model family.

## 6.6 Model registry extension

Extend model governance to support instrument models without weakening the Phase 5 separation registry.

Every production instrument model needs:

- model ID
- version/revision
- artifact SHA
- artifact size
- code license
- weight license
- commercial-use review
- redistribution review
- security review
- input sample rate
- input shape/window
- output vocabulary version
- supported providers
- benchmark/evaluation record
- confidence-calibration record

Do not ship a research model because it performs well if licensing is unclear.

## 6.7 ML research/training workspace

Add a non-runtime workspace:

```text
ml/
  README.md
  datasets/
    manifests/
    README.md
  instruments/
    training/
    evaluation/
    calibration/
    export/
  common/
```

Rules:

- do not commit copyrighted training audio
- commit dataset manifests/provenance, not proprietary source material
- pin training environment dependencies
- store experiment metadata
- separate train/validation/test identities
- maintain a frozen evaluation set
- maintain an intentionally difficult negative/unknown set
- record class imbalance
- record per-class metrics

## 6.8 Time-local detection

Instrument analysis must be event-based, not one global label per song.

`InstrumentEvent` should contain:

- taxonomy ID
- family
- source role
- confidence
- start/end time
- optional source/stem association
- active frequency regions
- attributes
- model provenance
- evidence references

Implement overlapping windows and event smoothing/hysteresis so labels do not flicker every analysis frame.

## 6.9 Hierarchical confidence calibration

Required behaviors:

- per-class thresholds
- parent-child probability consistency
- calibrated confidence rather than raw logits
- exact child label only when threshold passes
- otherwise fall back to parent
- explicit unknown state
- no forced winner when all classes are weak

Example:

```text
Rhodes 0.52      -> do not claim Rhodes
Electric piano 0.88 -> display electric piano
Keys 0.96       -> retained as parent evidence
```

## 6.10 Separation-assisted instrument evidence

Use Phase 5 stems as optional evidence, never as proof beyond their taxonomy.

Examples:

- bass detector can inspect the bass stem plus canonical stereo
- drum detector can inspect the drums stem plus canonical stereo
- vocal identity can use the vocal stem
- tonal instrument detector can use `other` only as a broad candidate source

Do not label `other` as piano/guitar/etc. without a real instrument model.

## 6.11 Instrument characteristics

For each supported event, extract where technically meaningful:

- fundamental/pitch track
- spectral centroid
- bandwidth
- harmonicity
- transient density
- attack time
- decay/sustain behavior
- envelope
- crest
- source loudness/activity
- stereo width/correlation
- active frequency regions
- distortion/saturation indicators

These characteristics become inputs to Phase 7+ interactions and repair decisions.

## 6.12 AnalysisReport integration

Add versioned instrument events to the analysis report.

Do not break old projects.

Requirements:

- schema version bump where necessary
- migration/default behavior for old reports
- absent instrument events are valid for old projects
- serialized model provenance

## 6.13 Desktop UI integration

Add a compact instrument summary to analysis details.

Display policy:

- high confidence: `Rhodes`
- medium child confidence but strong parent: `Electric piano`
- broad evidence only: `Keys / tonal source`
- uncertain: omit exact claim or show `Uncertain tonal source`

Never overwhelm the main mastering workflow with classifier diagnostics.

## 6.14 CLI tooling

Add commands such as:

```text
amt_cli instruments <input> [--json]
amt_cli instrument-events <input> [--json]
```

Useful for model evaluation and regression tests without the GUI.

## 6.15 Phase 6 tests

### Unit

- taxonomy parent/child integrity
- no cycles
- stable IDs
- fallback resolution
- confidence thresholds
- event merge/split logic
- serialization

### Model evaluation

- per-class precision/recall/F1
- macro/micro metrics
- confusion matrices
- unknown rejection
- calibration error
- family-level accuracy
- exact-label accuracy
- time-local onset/offset quality

### Integration

- canonical stereo only
- separated-stem assisted path
- no-separation fallback
- CPU-only inference
- cancellation
- corrupt model
- model version mismatch

### UI

- exact label only above policy threshold
- family fallback
- unknown behavior
- time-local entries reflected correctly

## Phase 6 exit criteria

- hierarchical taxonomy is versioned and tested
- instrument detector provider is replaceable
- production model has legal/provenance approval
- confidence calibration exists
- exact labels are suppressed when unsafe
- time-local events work
- priority beat-relevant classes are useful
- UI uses confidence-aware wording
- Phase 5 safety rules remain intact

---

# 7. Phase 7 — Kick/808 + Instrument Interaction Engine

## Implementation status (2026-09)

The repository now contains `src/interactions` with general pairwise interaction evidence, kick/808-ready masking/onset/phase/mono metrics, bounded repair recommendations, and damage guards. It does **not** yet connect real approved detector output to rendering or contain blind listening evidence, so Phase 7 is not accepted.

Target milestone: `0.7`

This phase converts instrument identity into better mastering decisions.

## 7.1 New interaction domain

Create:

```text
src/interactions/
  include/amt/interactions/
    InteractionGraph.h
    InteractionEvidence.h
    KickTracker.h
    BassTracker.h
    MaskingAnalyzer.h
    PhaseCoherenceAnalyzer.h
    LimiterContribution.h
    InteractionDiagnosis.h
    InteractionRepair.h
```

## 7.2 Kick analysis

Track:

- onset times
- peak/transient strength
- estimated fundamental/body region
- decay
- click region
- phase behavior
- section activity

## 7.3 808/bass analysis

Track:

- pitch/fundamental
- octave/harmonic content
- envelope
- sustained level
- distortion/harmonic density
- stereo width
- section activity

## 7.4 Pairwise interaction metrics

Implement:

- temporal overlap
- spectral overlap
- low-band masking
- onset masking
- phase/coherence risk
- limiter gain-reduction contribution estimate
- section-specific conflict
- mono survival
- small-speaker survival

## 7.5 Generalize beyond kick/808

Interaction graph should support later pairs:

- kick vs bass guitar
- kick vs synth bass
- vocal vs lead synth/guitar
- snare vs dense midrange
- hi-hat/cymbal vs vocal air
- brass vs upper-mid vocal presence
- bass vs stereo/mono compatibility

## 7.6 Bounded repair strategies

Prefer Mode 1/source-guided stereo approaches when possible.

Possible bounded actions:

- frequency-selective dynamic attenuation
- transient-preserving sidechain-style control
- low-band mono stabilization
- short-duration spectral ducking
- harmonic enhancement for bass translation
- source-aware EQ guidance on canonical stereo
- phase-safe low-frequency adjustments

No repair may assume reconstruction is safe merely because stems exist.

## 7.7 Damage guard

Before/after evaluation must measure:

- true peak
- loudness
- transient survival
- spectral side effects
- low-end balance
- mono compatibility
- width changes
- section consistency
- artifact evidence

## Phase 7 exit criteria

Blind testing must show targeted kick/808 problem mixes improve while already-good mixes remain protected by explicit damage-rate limits defined before evaluation.

---

# 8. Phase 8 — Advanced Mix Repair / Restoration

Target milestone: late `0.7` / early `0.8`

This phase is high-risk and therefore confidence-gated.

## 8.1 Add repair domain

Create:

```text
src/repair/
  RepairProposal.h
  RepairProvider.h
  RepairPolicy.h
  RepairValidator.h
  Declipping.h
  TransientRepair.h
  Denoise.h
  LocalArtifactReduction.h
  Dereverb.h
```

## 8.2 Repair classes

Implement only when evidence and validation justify them:

- declipping
- transient reconstruction
- localized denoise
- localized artifact reduction
- optional dereverb
- neural repair provider interface

## 8.3 Provider policy

Every repair provider must expose:

- capability
- model/version
- provenance
- confidence
- expected benefit
- artifact risk
- supported sample rates/channels
- CPU/GPU/cloud capabilities

## 8.4 Repair validation

Each repair must have:

1. pre-analysis
2. repair proposal
3. bounded render
4. post-analysis
5. artifact comparison
6. accept/reject decision
7. stereo fallback

## Phase 8 exit criteria

No advanced repair becomes automatic until predicted benefit exceeds measured artifact risk under a documented acceptance policy.

---

# 9. Phase 9 — Mastering Decision Engine v2

Target milestone: `0.8`

Move beyond the simple heuristic planner while preserving deterministic safety.

## 9.1 Add decision domain

Create:

```text
src/decision/
  Diagnosis.h
  Evidence.h
  MasteringConstraints.h
  CandidateGenerator.h
  CandidateOptimizer.h
  CandidateScorer.h
  CandidateRanker.h
  SafetyValidator.h
  ExplanationGenerator.h
```

## 9.2 Diagnosis model

A diagnosis must link:

- measurable evidence
- inferred source/instrument
- confidence
- severity
- time range
- proposed operations
- operation risk
- constraints

## 9.3 Candidate graph generator

Generate multiple internal candidate graphs, then select exactly two user-facing finalists.

Candidate space may vary:

- node inclusion
- node order
- thresholds
- EQ bands
- dynamic-EQ behavior
- transient shaping
- saturation
- clipper strategy
- limiter strategy
- stereo treatment
- source-guided interventions

## 9.4 Bounded optimizer

Do not use unconstrained black-box optimization over arbitrary DSP values.

Use:

- parameter bounds
- graph validity rules
- loudness/peak constraints
- transient constraints
- phase constraints
- operation-specific safety constraints

## 9.5 Candidate scorer

Score dimensions separately:

- tonal balance
- low-end stability
- punch/transient survival
- loudness/density
- distortion/artifact risk
- stereo/phase
- section consistency
- translation
- style fit
- reference fit when enabled
- personal preference when enabled

Keep individual components for explainability.

## 9.6 Learned ranker

A learned ranker is optional until enough high-quality pairwise human data exists.

Before then:

- deterministic rules + calibrated proxies remain authoritative
- do not train on unlabeled user selections as if they were universal truth

When introduced:

- use pairwise preference data
- keep model versioned
- preserve deterministic rejection rules after ranking
- never let ranker bypass hard safety gates

## 9.7 Recommendation explanation

Generate concise user-facing reasons from evidence, not generic templates disconnected from the actual plan.

Examples:

- better kick definition
- less 808 masking
- preserved snare transient
- lower upper-mid harshness
- better mono low-end stability
- more consistent hook loudness

## Phase 9 exit criteria

- two finalists are meaningfully different
- Master A is explicitly recommended
- Master B remains preservation-biased
- recommendation evidence is traceable
- recommended v2 system beats the Phase 2 deterministic baseline in predeclared blind tests
- damage rate on already-good mixes is bounded

---

# 10. Phase 10 — Reference Mastering + My Sound Profiles

Target milestone: `0.8`

## 10.1 Add reference domain

Create:

```text
src/reference/
  ReferenceAnalysis.h
  ReferenceProfile.h
  ReferenceInfluence.h
  ReferenceStore.h
```

## 10.2 Reference analysis

Derive reusable descriptors:

- broad spectral balance
- low-frequency distribution
- loudness/density
- PLR/crest
- macro dynamics
- transient character
- stereo-width curve
- saturation/distortion character
- brightness/air
- section contrast

## 10.3 Influence strengths

Support:

- Loose
- Medium
- Close

Even `Close` is bounded by source identity and safety constraints.

## 10.4 Saved profiles

Reference profiles store descriptors and provenance, not copyrighted reference audio by default.

Version:

- schema
- analyzer version
- model versions
- source fingerprint metadata where appropriate

## 10.5 My Sound profile aggregation

Allow named profiles to aggregate:

- multiple reference profiles
- chosen A/B history
- accepted revisions
- user description metadata

Example profile names are user-defined.

## Phase 10 exit criteria

Reference/profile influence is audible and measurable without blindly matching spectrum or destroying the source track's identity.

---

# 11. Phase 11 — Natural-Language Revision Engine

Target milestone: `0.8`

Natural language is a high-level control surface over a bounded mastering system.

## 11.1 Add revision domain

Create:

```text
src/revision/
  RevisionIntent.h
  RevisionParser.h
  TargetResolver.h
  ConstraintResolver.h
  PlanEditor.h
  RevisionValidator.h
  RevisionExplanation.h
```

## 11.2 Typed intent schema

Support operations such as:

- increase punch
- reduce harshness
- reduce bass level
- increase/decrease width
- increase grit/saturation
- make louder
- preserve transient
- preserve named instrument
- reference/profile influence adjustment

Targets may be:

- global
- instrument
- source family
- time range
- structural section

## 11.3 Deterministic common-command parser

Implement high-frequency commands locally without requiring an LLM.

Examples:

- `make the drums hit harder`
- `the hats are too sharp`
- `back the 808 down`
- `more width, don't touch the bass`
- `make it louder without flattening the snare`

## 11.4 Optional language-model provider

Add a provider abstraction for complex language parsing.

The provider outputs only typed intent; it never writes raw DSP parameters directly.

All outputs pass deterministic validation.

## 11.5 Target resolver

Resolve terms against the current analysis:

- `808`
- `bass`
- `drums`
- `brass`
- `hook`
- `second verse`
- `everything except vocals`

If the requested target is not detected confidently, say so instead of silently changing another source.

## 11.6 Constraint preservation

Constraints such as:

- don't touch bass
- preserve snare
- leave vocals alone
- only in the hook

must survive planning and be recorded in history.

## 11.7 Revision history

Every accepted revision creates a new append-only revision node with:

- parent revision
- text request
- typed intent
- target resolution
- plan delta
- render provenance
- explanation

## Phase 11 exit criteria

Core natural-language commands reliably change the intended property/source while explicit constraints are obeyed and testable.

---

# 12. Phase 12 — Playback Translation Engine

Target milestone: `0.8`

## 12.1 Add translation domain

Create:

```text
src/translation/
  PlaybackClass.h
  TranslationModel.h
  TranslationAnalyzer.h
  TranslationScore.h
  InstrumentSurvival.h
```

## 12.2 Playback classes

Initial generic classes:

- studio monitors
- headphones
- earbuds
- phone speaker
- laptop speaker
- small Bluetooth speaker
- car
- mono
- large PA/club-style system

Do not claim exact branded-device emulation.

## 12.3 Analysis

Combine:

- transfer-function filtering
- bandwidth limits
- mono/stereo behavior
- bass audibility
- small-speaker nonlinear limitations where safely modeled
- instrument survival

## 12.4 Candidate scoring integration

Translation results become one input to the decision engine, not an unconditional target.

Avoid sacrificing full-range quality solely to improve tiny-speaker scores.

## Phase 12 exit criteria

Translation warnings correspond to reproducible weaknesses, and using translation in candidate scoring improves weak playback cases without unacceptable full-range degradation.

---

# 13. Phase 13 — Preference Learning

Target milestone: `0.9`

## 13.1 Add preference domain

Create:

```text
src/preferences/
  PreferenceEvent.h
  PreferenceStore.h
  PreferenceVector.h
  PreferenceModel.h
  ProfilePreferences.h
```

## 13.2 Events

Capture locally:

- A/B selection
- rejected candidate
- accepted revision
- repeated revision pattern
- reference/profile usage
- preferred loudness/dynamics trade-off

## 13.3 Bound influence

Preference learning must not override safety or rewrite the base mastering logic.

Use bounded influence over:

- candidate ranking
- style weights
- loudness preference
- saturation preference
- width preference
- intervention aggressiveness

## 13.4 Controls

Required:

- disable learning
- reset
- export
- import
- per-profile separation

## 13.5 Privacy

Raw audio is not global training data without explicit consent.

Preference records must be separable from any future opt-in global learning system.

## Phase 13 exit criteria

Preference adaptation is measurable, bounded, reversible and versioned, and old project results remain reproducible from their stored provenance.

---

# 14. Phase 14 — Album / Beat-Tape Batch Mastering

Target milestone: `0.9`

## 14.1 Add batch domain

Create:

```text
src/batch/
  BatchProject.h
  BatchQueue.h
  CollectionAnalysis.h
  CohesionPlanner.h
  SequenceAudition.h
  BatchExport.h
```

## 14.2 Workflow

1. ingest tracks
2. analyze each track independently
3. compute collection statistics
4. identify intentional contrast
5. establish cohesion targets
6. master each track under track + collection constraints
7. re-evaluate sequence
8. allow sequence audition
9. export all tracks and project report

## 14.3 Cohesion dimensions

- perceived loudness
- bass weight
- brightness
- stereo image
- transient density
- saturation
- macro dynamics
- tonal identity

## 14.4 Queue behavior

Required:

- cancellation
- pause/resume where practical
- per-track failure isolation
- cache reuse
- clear progress
- no project corruption on crash/restart

## 14.5 Album loudness philosophy

Do not flatten all tracks to identical LUFS.

Preserve intentional sequencing and dynamics while preventing accidental track-to-track mismatches.

## Phase 14 exit criteria

A collection sounds cohesive while intentional contrast survives, and one failed track does not destroy the batch job or its project state.

---

# 15. Phase 15 — Windows Standalone Hardening and 1.0

Target milestone: `0.9` -> `1.0.0`

This phase turns a capable mastering engine into a distributable product.

## 15.1 Desktop UX consolidation

By this point the desktop must expose the full user journey cleanly:

- Home / ingest
- optional reference
- recent projects
- Analyze
- findings
- detected instruments
- Master A / Master B
- recommendation explanation
- synchronized loudness-matched audition
- revision field
- history
- export
- batch mode
- settings/model/cache management

No engineering-control wall is added.

## 15.2 Host-framework checkpoint

Do not rewrite the proven engine merely to chase a framework.

Before major final UI polish, explicitly decide whether Windows `1.0` remains on the existing native Win32 host or migrates to a cross-platform host.

Decision criteria:

- release stability
- accessibility
- high DPI
- localization readiness
- future VST3 reuse
- licensing
- implementation risk

If migration does not clearly improve `1.0`, keep the Windows host and keep the core wrapper-independent.

## 15.3 Settings

Add:

- audio output device
- local/cloud mode
- CPU/GPU preference where supported
- model storage location where safe
- cache size policy
- cache clear
- privacy controls
- telemetry/crash-report opt-in/out
- update channel if implemented
- default export recipe
- accessibility options

## 15.4 Model manager

User-facing model management must show:

- installed model
- version
- size
- provider availability
- verification state
- download progress
- repair/redownload
- remove cached model

Do not expose untrusted arbitrary model URLs in the production UI.

## 15.5 Cache manager

Add:

- cache inventory
- size
- safe eviction
- age/usage policy
- project-owned vs disposable distinction
- no deletion of canonical user source files

## 15.6 Codec completion

Review the original format targets and finish release-supported compressed formats under an explicit licensing strategy.

Candidate release support should include where legally/technically validated:

- WAV
- AIFF
- FLAC
- MP3
- AAC/M4A
- OGG/Vorbis
- Opus

Do not enable codec libraries/build flags without license review.

## 15.7 Export completion

Validate recipes:

- Studio Master — 24-bit WAV
- Distribution Master — WAV/FLAC
- CD — 16-bit/44.1 kHz with one final dither stage
- Client Preview — high-quality compressed preview
- Archive — master + manifest + analysis/report metadata

Final render must perform post-render true-peak/loudness validation.

## 15.8 Installer

Deliver a clean-machine Windows installer.

Requirements:

- correct runtime dependencies
- application binaries
- ONNX Runtime DLLs
- registry/model metadata
- no giant model embedded unless redistribution and installer size policy explicitly approve it
- per-user model download still works
- upgrade path preserves projects/preferences
- uninstall does not destroy user projects

## 15.9 Updater

If automatic update is included:

- signed release metadata
- version checks
- download integrity verification
- rollback/repair behavior
- do not update while an active mastering render is modifying project state

If not ready for 1.0, provide a clear manual-update path rather than shipping an unsafe updater.

## 15.10 Crash recovery

Persist enough job/project state to recover from:

- app crash
- worker crash
- forced restart
- interrupted batch
- cancelled render

Never present partial output as a valid final master.

## 15.11 Accessibility / input

Validate:

- keyboard navigation
- visible focus
- scalable UI/high DPI
- screen-reader labels where supported
- contrast
- non-mouse operation for main workflow

## 15.12 Security/privacy

Review:

- model download trust boundary
- worker command-line escaping
- temporary file ownership
- canonical source immutability
- project path traversal
- malformed project data
- cloud upload consent
- crash-report redaction
- log redaction

## 15.13 Performance

Establish benchmark classes:

- CPU-only supported machine
- optional NVIDIA acceleration path when approved
- low-memory system
- long stereo files
- common sample rates

Measure:

- analysis speed
- mastering render speed
- separation speed
- peak RAM
- cache size
- startup
- UI responsiveness

Performance regressions become release blockers when they materially affect ordinary workflows.

## 15.14 Windows QA matrix

Test:

- clean Windows install
- no dedicated GPU
- GPU available but disabled
- low VRAM
- long songs
- short clips
- mono sources
- unusual sample rates
- unicode filenames
- long paths where supported
- read-only source folders
- malformed/corrupted media
- cancelled operations
- offline mode
- missing internet
- interrupted model download
- reopened old projects
- corrupted optional cache

## 15.15 Release listening benchmark

Build a frozen release corpus containing:

- already-good mixes
- bass-heavy beat/instrumental mixes
- vocal songs
- bright/harsh mixes
- transient-heavy drums
- dark mixes
- wide low end
- clipped/saturated character
- clean acoustic material
- edge cases

For each sound-changing major feature:

- blind evaluation
- loudness matching
- damage flags
- targeted-benefit metrics
- thresholds declared before final acceptance

## 15.16 Release provenance

Every release must preserve:

- app version
- DSP version
- model versions
- model hashes
- analysis schema
- mastering graph schema
- project schema
- dependency/license notices

## Windows 1.0 exit criteria

Windows `1.0.0` is ready only when:

- clean build succeeds
- clean-machine install succeeds
- normal user can master end-to-end
- Phase 5 real runtime acceptance is complete
- instrument identification is confidence-safe
- interaction repairs are damage-gated
- two-master recommendation system clears the defined listening benchmark
- natural-language revisions obey targets/constraints
- reference/profile flow works
- translation scoring is validated
- batch mode is reliable
- project/history reopen works
- no known data-loss issue exists
- loudness/true-peak measurements remain conformant
- canonical source remains immutable
- license/provenance documentation is complete
- installer/uninstaller preserve user data correctly
- CPU-only fallback remains usable

---

# 16. Phase 16 — VST3 / ARA-Capable Architecture

Target milestone: `1.1`

Do this only after the Windows standalone engine is accepted.

## 16.1 Plugin wrapper

Create a thin plugin host around shared application/core services.

Plugin must not duplicate mastering logic.

## 16.2 Realtime safety

Audio thread must never perform:

- network I/O
- model downloads
- model initialization
- stem separation
- large ML inference
- unbounded allocation
- blocking file I/O
- project migrations

## 16.3 Offline advanced workflow

Advanced operations run through offline jobs and render/bounce workflows.

## 16.4 State recall

Plugin state stores references/versioned parameters needed to recall the project safely without embedding huge caches into the DAW state chunk.

## 16.5 ARA-ready contracts

Design whole-song analysis access so future ARA integration can be added without changing the mastering engine.

## Phase 16 exit criteria

- VST3 validator passes
- common DAW smoke tests pass
- state recall works
- realtime thread remains bounded and ML-free

---

# 17. Phase 17 — Web Application

Target milestone: `1.2`

## 17.1 Frontend

Create:

```text
web/
  app/
  components/
  audio/
  api/
  workers/
```

Use a web UI that mirrors the product workflow, not the Windows implementation details.

## 17.2 Shared core

Move deterministic portable components to WebAssembly where practical.

Candidate WASM areas:

- analysis utilities
- deterministic DSP subsets
- graph serialization
- project contracts
- scoring utilities

## 17.3 Browser inference

Use capability negotiation:

- WebGPU/browser local when model and runtime are validated
- CPU/WASM for small compatible work
- cloud fallback for large or unsupported models

Do not assume HTDemucs or future instrument models belong in every browser.

## 17.4 Project synchronization

Define explicit sync/version/conflict rules rather than silently making local Windows projects cloud-native.

## Phase 17 exit criteria

The web product performs the same core user journey with explicit capability fallback and no forked mastering logic.

---

# 18. Phase 18 — macOS and Linux

Target milestones: `1.3` / `1.4`

Port the core first, then host/platform integrations.

## 18.1 macOS

Validate:

- Apple Silicon
- Intel only if still required
- CoreAudio
- filesystem/path semantics
- model provider support
- installer/package
- code signing
- notarization

## 18.2 Linux

Choose supported distributions rather than claiming universal Linux support.

Validate:

- compiler/toolchain
- audio backend
- packaging format
- model runtime providers
- GPU support policy
- filesystem permissions

## Phase 18 exit criteria

No platform-specific host fork may modify mastering behavior to compensate for architecture drift. Shared-core audio regression output should stay within defined platform tolerances.

---

# 19. Cross-Cutting Architecture Work

These are continuous requirements, not separate optional phases.

## 19.1 Versioned contracts

Version all persistent contracts:

- project
- analysis
- instrument taxonomy
- instrument events
- source diagnostics
- reference profile
- mastering graph
- revision intent
- preference profile
- batch project
- model manifest

Add migration code before incompatible schema changes ship.

## 19.2 Application service layer

Introduce stable services shared by desktop/CLI/plugin/web adapters:

```text
src/application/
  AnalyzeTrackService
  MasterTrackService
  RevisionService
  ReferenceService
  BatchMasterService
  ModelService
  ProjectService
```

The UI should orchestrate services, not instantiate low-level DSP/model objects directly.

## 19.3 Job system

Standardize job behavior:

- unique job ID
- state
- progress
- cancellation
- result
- error
- provenance
- resumability where applicable

Heavy inference remains outside the UI process.

## 19.4 Logging

Structured logs should distinguish:

- measurement
- model inference
- policy decision
- fallback
- user action
- render action
- warning/error

Do not log sensitive source paths or user text to remote services without explicit policy.

## 19.5 Determinism

Deterministic DSP/mastering paths should remain reproducible for the same:

- source fingerprint
- plan
- DSP version
- settings

ML outputs that are not bit deterministic still require model/version/provenance reproducibility.

## 19.6 Error taxonomy

Replace generic string-only failure handling at major boundaries with typed categories where useful:

- source I/O
- decode
- analysis
- inference/model
- cache
- mastering
- export
- project
- cancellation
- network/cloud

User-facing text can remain simple while internal diagnostics stay structured.

---

# 20. Testing Strategy for the Remainder of the Project

## 20.1 Unit tests

Every domain contract and algorithm should receive unit coverage.

## 20.2 Integration tests

Test full flows:

- analyze -> plan -> render
- analyze -> separation -> diagnostics -> render
- analyze -> instruments -> interaction -> render
- reference -> plan
- natural language -> typed intent -> plan delta
- project save/reopen
- batch queue

## 20.3 Audio golden/regression tests

Maintain curated source fixtures with expected constraints rather than brittle exact floating-point output when algorithms intentionally evolve.

Check:

- no NaN/Inf
- bounded peak
- expected loudness range
- channel geometry
- no unexpected silence
- no DC regression
- no excessive mono loss
- transient survival where relevant

## 20.4 Property/fuzz tests

Use fuzz/property testing for:

- project JSON
- model registry parsing
- worker messages
- taxonomy parsing
- revision intent parsing
- malformed audio metadata boundaries

## 20.5 Model tests

Every production model needs:

- frozen evaluation set
- metrics
- calibration report
- provider/runtime smoke test
- CPU fallback test where required
- artifact hash verification
- license/provenance approval

## 20.6 Listening tests

Any feature that changes sound must have listening validation before automatic activation.

Use:

- loudness-matched blind A/B
- already-good controls
- targeted problem mixes
- damage flags
- category stratification
- predeclared acceptance policy

---

# 21. Model and Data Governance

## 21.1 No opaque production models

Every production model must be traceable to:

- architecture/source
- weights source
- revision
- hash
- license
- commercial status
- redistribution status
- benchmark
- calibration
- security review

## 21.2 Model replacement

Business logic must operate against capabilities/contracts so a model can be replaced without rewriting the mastering product.

## 21.3 Training data

Track dataset provenance and usage rights.

Do not commit third-party training corpora into ordinary Git history.

## 21.4 User data

User audio and preference records remain private by default and are not assumed to be global training material.

---

# 22. Proposed Repository Additions

As phases are implemented, the repository should evolve toward:

```text
src/
  application/
  analysis/
  app/
  audio/
  batch/
  codec/
  core/
  decision/
  dsp/
  instruments/
  interactions/
  mastering/
  playback/
  preferences/
  project/
  reference/
  repair/
  revision/
  separation/
  translation/
  worker/

ml/
  common/
  datasets/
  instruments/
  ranking/

models/
  registry.json
  manifest.schema.json
  ...

tests/
  ...

docs/
  ...

web/
  ...                 # Phase 17

plugin/
  ...                 # Phase 16 if a separate wrapper tree is preferred
```

Do not create all empty directories up front. Add them when their phase begins.

---

# 23. Immediate Ordered Backlog

These are the next concrete tasks in order.

## Phase 5 acceptance

- [ ] Run `windows-msvc` configure.
- [ ] Fix native compiler/linker errors.
- [ ] Build Release.
- [ ] Run full CTest locally.
- [ ] Validate registry script locally.
- [ ] Cold-start trusted HTDemucs installation.
- [ ] Verify model SHA/size/location.
- [ ] Verify ONNX Runtime DLL loading.
- [ ] Verify four-stem inference on real audio.
- [ ] Verify source issue inference reaches desktop.
- [ ] Verify automatic Mode 1 remains gated.
- [ ] Verify cache hit and invalidation.
- [ ] Verify missing worker fallback.
- [ ] Verify missing/corrupt model fallback.
- [ ] Verify cancellation cleanup.
- [ ] Verify diagnostic sidecar persistence.
- [x] Implement diagnostic sidecar reload on project reopen.

## Architecture cleanup

- [ ] Add stable desktop application/session controller.
- [ ] Extract job launch/cancellation from Win32 event handlers.
- [ ] Extract project state mutation from widget code.
- [ ] Replace Phase 5 macro routing with a real service call.
- [ ] Collapse to one authoritative desktop entry point.
- [ ] Preserve existing UX while refactoring.

## Phase 6 foundation

- [x] Add `src/instruments` target.
- [x] Implement taxonomy schema and validation.
- [x] Implement stable instrument IDs and parent hierarchy.
- [x] Add `InstrumentEvent` contract.
- [x] Add instrument detector provider abstraction.
- [ ] Extend model registry schema for instrument model capability.
- [ ] Create `ml/instruments` evaluation/training structure.
- [x] Define frozen Phase 6 evaluation corpus manifest.
- [ ] Benchmark candidate instrument models.
- [ ] Complete model license/provenance review.
- [ ] Implement time-local inference windows.
- [ ] Implement event smoothing/hysteresis.
- [ ] Implement calibrated thresholds.
- [ ] Implement hierarchical fallback.
- [ ] Implement explicit unknown behavior.
- [ ] Integrate optional Phase 5 stem evidence.
- [ ] Add instrument characteristics.
- [ ] Serialize instrument events into analysis.
- [ ] Add CLI JSON output.
- [ ] Add confidence-aware desktop summary.
- [ ] Add full Phase 6 unit/model/integration tests.

Do not jump to Phase 7 repairs until Phase 6 identity confidence is trustworthy.

---

# 24. Release Milestone Map

| Version | Required capability |
|---|---|
| `0.5.x` | Phase 5 runtime accepted; source diagnostics/source-guidance foundation |
| `0.6` | hierarchical time-local instrument intelligence |
| `0.7` | kick/808 + interaction-aware repair; advanced repair foundations |
| `0.8` | decision engine v2 + reference profiles + natural language + translation |
| `0.9` | preference learning + album mode + Windows release hardening |
| `1.0.0` | production-quality Windows standalone |
| `1.1` | VST3 / ARA-capable integration |
| `1.2` | web application |
| `1.3` | macOS |
| `1.4` | Linux |

Version labels are subordinate to acceptance criteria. Do not bump a milestone simply because code exists.

---

# 25. Definition of Done for Every Major Feature

A feature is complete only when it has all applicable items:

- [ ] implementation
- [ ] stable domain/API contract
- [ ] unit tests
- [ ] integration tests
- [ ] cancellation behavior
- [ ] error/fallback behavior
- [ ] serialization/versioning if persistent
- [ ] logging/diagnostics
- [ ] performance measurement
- [ ] model/dependency provenance review
- [ ] security/privacy review where relevant
- [ ] documentation
- [ ] user-facing integration
- [ ] listening validation when it changes sound
- [ ] backward compatibility consideration

Producing output is not sufficient.

---

# 26. Final Project Acceptance

The complete original roadmap is achieved when:

1. Windows `1.0` satisfies its full acceptance gate.
2. VST3 wrapper passes realtime-safety and recall validation.
3. Web uses the same mastering contracts with capability-based local/cloud execution.
4. macOS and Linux ports preserve shared-core behavior.
5. Instrument labels are confidence-calibrated and hierarchical.
6. source-specific diagnoses are evidence-based.
7. repair operations are benefit-vs-artifact gated.
8. Master A/B remain meaningfully different and useful.
9. the recommended candidate wins the declared listening benchmark at an acceptable rate.
10. already-good mixes are protected by explicit damage thresholds.
11. natural-language revision targets are typed, validated and constrained.
12. reference and personal profiles influence character without blind matching.
13. preference learning is local-first, reversible and bounded.
14. batch mastering preserves collection cohesion and intentional contrast.
15. project history remains reopenable with model/DSP provenance.
16. canonical user source audio is never destructively edited.
17. CPU fallback remains viable wherever the product contract requires it.
18. dependency/model licensing is documented for every shipped release.
19. no platform wrapper owns unique mastering logic.
20. `main` remains the complete authoritative repository rather than one of several competing branches.

---

# 27. Next Development Command

The next implementation session should begin with:

> Continue `DocDamage/audiomasteringtool` from `main`. Do not create another long-lived phase branch. Finish Phase 5 Windows runtime acceptance first, including real HTDemucs startup, cache/fallback validation and project-sidecar restore. Then consolidate the desktop Phase 4/5 wrapper into stable application/session controllers so future features do not require `Phase6Main.cpp`-style macro routing. Once Phase 5 is truthfully accepted, begin Phase 6 hierarchical instrument intelligence with a dedicated `src/instruments` domain, versioned taxonomy, time-local `InstrumentEvent` contract, confidence calibration, explicit unknown/fallback behavior, replaceable production-model interface, model licensing/provenance gates, and beat-first instrument classes. Do not begin automatic interaction repair until instrument confidence is validated.
