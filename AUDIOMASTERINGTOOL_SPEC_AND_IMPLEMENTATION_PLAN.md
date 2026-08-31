# AudioMasteringTool — Product Specification & Implementation Plan

**Repository:** `DocDamage/audiomasteringtool`  
**Status:** Authoritative initial specification  
**Primary first release target:** Windows standalone desktop application  
**Future targets:** VST3, web, macOS, Linux, and other practical plugin/app formats  

---

## 1. Product Definition

AudioMasteringTool is an autonomous **audio finishing engine**, not a conventional mastering plugin and not a DAW.

Its primary promise is:

> **Drop a track in. Get a finished master.**

The application must analyze the uploaded mix, understand the musical content and specific instruments where possible, diagnose mix/mastering problems, repair those problems when appropriate, create two high-quality mastering candidates, recommend one, provide loudness-matched A/B auditioning, accept natural-language revisions, and export a finished master with minimal user effort.

The system is allowed to use traditional DSP, machine learning, neural audio processing, source separation, restoration, instrument-aware processing, reference analysis, and cloud acceleration when those methods improve the result.

The goal is not to imitate a particular vendor's internal implementation. The goal is to provide the same class of effortless **"make this sound finished"** experience while developing a deeper instrument-aware and repair-aware mastering system.

---

## 2. Locked Product Decisions

### 2.1 Default workflow

1. User drops an audio file into the application.
2. Application analyzes the entire track before mastering.
3. Application detects style/genre automatically.
4. Application identifies specific instruments and production elements whenever confidence is sufficient.
5. Application diagnoses technical and perceptual problems.
6. Application decides whether stereo-only processing is sufficient or whether source-aware/stem-aware repair is beneficial.
7. Application generates **two** mastering candidates.
8. One candidate is explicitly marked **Recommended**.
9. A preservation-biased alternative is always available.
10. User auditions Original / Master A / Master B with automatic loudness matching.
11. User may accept the recommendation, choose the alternative, or request a revision in natural language.
12. Final export uses maximum-quality rendering.
13. Projects, analyses, revisions, references, candidates, profiles, and exports are retained in project history.

### 2.2 Target material

The system must handle general music, including complete vocal songs, but its first-class optimization target is **beats and instrumentals**, especially material where drum impact, bass/808 behavior, sample character, clipping, saturation, and intentional grit are important.

### 2.3 Automatic vs manual control

The primary application is intentionally **not** a mastering workstation full of engineering controls.

There is no requirement for an advanced EQ/compressor/limiter panel in the main product. The user controls the system through:

- source track
- optional reference track/profile
- candidate selection
- simple high-level choices where necessary
- natural-language revision instructions
- export settings/recipes

The complicated engineering decisions remain internal.

### 2.4 Candidate behavior

The engine generates exactly two default candidates:

- **Master A — Recommended:** the engine's preferred solution for the track.
- **Master B — Alternative:** a lower-risk/preservation-biased alternative that deliberately uses a meaningfully different mastering interpretation where appropriate.

The candidates may use completely different internal processing graphs. They must not merely be two limiter thresholds.

### 2.5 Loudness philosophy

Default target: **modern commercial loudness**, selected intelligently per source rather than forcing every track to a single LUFS value.

The system must treat loudness as one optimization target among several:

- punch
- transient survival
- distortion
- tonal balance
- low-frequency stability
- translation
- genre/style expectations
- true peak
- perceived density

The system may choose limiter-only, clipping + limiting, multistage clipping + limiting, or more conservative dynamics when the source calls for it.

### 2.6 Local/cloud behavior

Architecture is local-first, with automatic cloud processing permitted when it materially improves quality or enables a model that is impractical locally.

Required modes:

- **Auto:** local when practical; cloud when quality/performance warrants it.
- **Offline/local only:** never upload audio.
- **Cloud preferred:** use higher-quality remote models when available.

The UI must always indicate whether a job is local or cloud-backed.

### 2.7 Platform order

1. Windows standalone first.
2. VST3 after the standalone mastering engine is validated.
3. Web application using shared model/DSP contracts.
4. macOS.
5. Linux.
6. Other practical plugin formats later.

The core engine must remain platform-independent so this order does not require rewriting mastering logic.

---

# 3. Product Goals

## 3.1 Primary goals

The product SHALL:

- Turn a stereo mix into a release-ready master with extremely low user effort.
- Diagnose the track before processing it.
- Understand musical structure rather than only aggregate spectral statistics.
- Identify specific instruments whenever possible.
- Be time-aware: instruments may enter/leave, change timbre, or serve different roles in different sections.
- Distinguish intentional production character from defects.
- Repair mix problems when necessary rather than merely pushing the mix through a generic mastering chain.
- Use source separation when useful, but avoid separation artifacts when source-aware control can be derived without reconstructing the mix.
- Produce two genuinely useful mastering alternatives.
- Explain recommendations in plain language.
- Support optional reference mastering.
- Learn the user's mastering preferences over time.
- Support natural-language revision commands.
- Support individual tracks and album/beat-tape batch mastering.
- Optimize for translation across multiple playback classes.
- Preserve input sample rate by default.
- Use a lossless/high-precision internal pipeline.
- Support CPU fallback and GPU acceleration.

## 3.2 Non-goals

The product SHALL NOT become:

- a multitrack DAW
- a MIDI sequencer
- a beat-making workstation
- a synthesizer host
- a songwriting tool
- a generative song creation service
- a traditional manual mastering suite
- a generic audio editor

Features that do not improve **analysis, repair, mastering, auditioning, revision, or export** should be treated as scope expansion and require explicit justification.

---

# 4. User Experience Specification

## 4.1 Home / ingest screen

Required elements:

- Large drag/drop target.
- File chooser fallback.
- Supported format summary.
- Optional reference drop target.
- Recent projects.
- Batch/album mode entry point.
- Settings entry point.

The normal single-track workflow should require no configuration before analysis.

## 4.2 Analysis screen

Required:

- waveform overview
- playback transport
- analysis progress/status
- detected genre/style
- detected musical structure
- plain-English findings
- mix-health dimensions
- detected key instruments/elements with confidence-aware wording
- warnings only when actionable

Example findings:

- `808 is masking the kick during the hook.`
- `Upper mids become harsh when the brass layer enters.`
- `The snare transient is being flattened by the current mix bus.`
- `The low end is wide below 90 Hz and may translate poorly in mono.`
- `The mix is already well balanced; only light finishing is recommended.`

## 4.3 Candidate screen

Required:

- Original / A / B selector
- Master A card
- Master B card
- Recommended badge
- brief explanation for recommendation
- loudness-match toggle, ON by default
- waveform/playhead shared across all versions
- synchronized switching without losing position
- artifact-safe crossfades when switching
- natural-language revision input
- Export button

No manual EQ, compressor, dynamic-EQ, clipper, or limiter controls are required in the normal UI.

## 4.4 Recommendation explanation

The engine should explain why a candidate is recommended using concise perceptual language, for example:

- Better kick definition
- Cleaner sub-bass translation
- Preserved snare transient
- Less upper-mid harshness
- Better mono compatibility
- More consistent section-to-section loudness

The UI must not imply that subjective quality is an objective scientific score.

## 4.5 Revision workflow

Natural-language commands are first-class controls.

Examples:

- `Make the drums knock harder.`
- `The hats got too sharp.`
- `Back the 808 down a little.`
- `More width, but don't touch the bass.`
- `Make it louder without flattening the snare.`
- `Give it more grit.`
- `Make this closer to my Boom Bap profile.`
- `Leave everything else alone and fix the harsh brass.`

Revision requirements:

- reuse existing analysis wherever valid
- preserve explicit user constraints
- create a new revision node rather than destructively replacing history
- display what changed in plain language
- allow revert to any prior candidate/revision

---

# 5. Audio Input / Output Requirements

## 5.1 Input

Target support:

- WAV
- AIFF/AIF
- FLAC
- MP3
- AAC/M4A where decoder support permits
- OGG/Vorbis
- Opus
- other practical formats through a controlled codec layer

The codec layer must be replaceable because licensing differs by codec/build configuration.

## 5.2 Length

The application should be effectively unrestricted for ordinary music use.

Implementation must therefore be chunk/stream capable and must not require loading an entire hour-long source into contiguous RAM.

## 5.3 Sample rates

Support practical rates from standard consumer rates through 192 kHz and higher where the decoder/container safely supports them.

Default export behavior: preserve the source sample rate unless an export recipe explicitly requests conversion.

## 5.4 Internal precision

- Decode/processing bus: at least 32-bit float.
- Precision-sensitive analysis/DSP may use 64-bit internally.
- No lossy intermediate renders.
- Stem caches and intermediate bounces must be lossless floating-point or mathematically equivalent.

## 5.5 Export

Required:

- WAV
- FLAC
- MP3
- alternate master
- optional processed stems when stem processing was used
- configurable export recipes

Recommended built-in recipes:

- Studio Master — 24-bit WAV
- Distribution Master — configurable WAV/FLAC
- CD — 16-bit / 44.1 kHz with dither
- Client Preview — high-quality MP3
- Archive — lossless master + project manifest + optional analysis report

Dither is applied only when reducing integer bit depth and never repeatedly across internal stages.

---

# 6. Standards and Metering Baseline

The metering layer must implement or validate against the current ITU loudness/true-peak algorithm family.

Baseline standard:

- **ITU-R BS.1770-5** for programme loudness and true-peak measurement.

EBU R 128 may be supported as a reporting/export preset, but commercial music mastering must not blindly treat broadcast programme loudness as the mastering target.

Required measurements include:

- integrated loudness
- short-term loudness
- momentary loudness
- loudness range where meaningful
- sample peak
- true peak
- crest factor
- peak-to-loudness ratio
- section-level loudness variation
- limiter/clipper contribution metrics

Meter calculations require conformance/golden-vector tests.

---

# 7. Analysis Engine

## 7.1 Analysis philosophy

The analyzer is not a single neural network. It is a graph of deterministic measurements and learned models whose outputs are fused into an `AnalysisReport`.

The system must keep separate concepts for:

- measurable fact
- model inference
- confidence
- user preference
- mastering recommendation

Example:

- Fact: low-band energy increased by 5.3 dB in the hook relative to the verse.
- Inference: dominant source is likely an 808.
- Confidence: 0.91.
- Diagnosis: 808 is likely driving limiter gain reduction and masking kick attack.
- Action: consider source-aware low-frequency dynamic control.

## 7.2 Analysis passes

### Pass A — file/technical integrity

- container/codec validation
- sample rate
- bit depth where available
- channels/layout
- duration
- DC offset
- NaN/Inf checks
- digital clipping indicators
- repeated full-scale samples
- inter-sample peak risk
- silence/head/tail detection
- channel imbalance

### Pass B — global perceptual descriptors

- integrated/short-term/momentary loudness
- true peak
- spectral distribution
- spectral centroid/rolloff
- low/mid/high energy ratios
- macro dynamics
- transient density
- stereo correlation
- band-limited stereo width
- phase stability
- mono compatibility
- tonal/key estimates
- tempo/beat grid confidence
- section boundaries

### Pass C — structural analysis

Detect likely:

- intro
- verse
- pre-hook
- hook/chorus
- bridge
- breakdown
- outro
- drops/builds where relevant

Labels should be probabilistic. The engine only requires reliable section segmentation; exact song-form naming is secondary.

### Pass D — instrument and source analysis

Run time-local multi-label instrument/source detection and, when justified, source separation or mask estimation.

### Pass E — interaction analysis

Analyze relationships such as:

- kick vs 808
- kick vs bass guitar/synth bass
- vocal vs lead instrument
- snare vs dense midrange material
- cymbal/hat harshness vs vocal air
- bass width vs mono compatibility
- limiter pumping vs low-frequency events

### Pass F — diagnosis

Fuse evidence into actionable problems and assign:

- severity
- confidence
- affected time ranges
- affected source/instrument
- candidate fixes
- risk of intervention

---

# 8. Specific Instrument Identification

## 8.1 Requirement

The system must not stop at vague roles such as `bass`, `melody`, and `drums` when more specific identification is reasonably possible.

It must attempt hierarchical identification and gracefully fall back when uncertain.

## 8.2 Hierarchical taxonomy

### Level 0 — source role

- percussion
- tonal instrument
- bass source
- voice
- ambience/effects
- unknown

### Level 1 — family

Examples:

- drums
- percussion
- bass
- keyboards
- guitars
- strings
- brass
- woodwinds
- synths
- vocals
- sampled material
- effects

### Level 2 — instrument

Initial target vocabulary should include at minimum:

#### Low end

- 808/sub-bass
- synth bass
- electric bass guitar
- acoustic/upright bass
- Reese-style bass
- distorted bass

#### Keyboards

- acoustic/grand piano
- upright piano
- Rhodes-style electric piano
- Wurlitzer-style electric piano
- clavinet
- Hammond/organ
- generic organ
- harpsichord

#### Guitars/plucked strings

- acoustic guitar
- clean electric guitar
- distorted electric guitar
- muted guitar
- nylon-string guitar
- banjo
- mandolin
- harp

#### Orchestral strings

- violin
- viola
- cello
- double bass
- string ensemble
- pizzicato strings

#### Brass

- trumpet
- trombone
- French horn
- tuba
- brass ensemble

#### Woodwinds

- saxophone
- flute
- clarinet
- oboe
- bassoon
- woodwind ensemble

#### Synth sources

- synth lead
- synth pad
- synth pluck
- synth keys
- bell/chime synth
- arpeggiated synth
- texture/drone

#### Drum kit

- kick
- snare
- clap
- rimshot/rim
- closed hi-hat
- open hi-hat
- crash cymbal
- ride cymbal
- toms

#### Percussion

- shaker
- tambourine
- conga
- bongo
- cowbell
- claves
- triangle
- hand percussion
- miscellaneous percussion

#### Voice

- lead vocal
- backing vocal
- ad-lib
- choir/group vocal
- spoken vocal
- vocal chop/sample

#### Sample/production elements

- vinyl/sample texture
- orchestral sample
- chopped melodic sample
- noise/foley
- riser
- impact
- reverse effect
- ambience

### Level 3 — subtype / production character

Where models support it, attach non-exclusive attributes rather than forcing them into the instrument name:

- acoustic / electric / synthetic
- clean / distorted / saturated
- dry / reverberant
- mono / stereo
- bright / dark
- short / sustained
- sampled / synthesized likelihood
- transient / sustained
- warm / aggressive / lo-fi character probabilities

## 8.3 Time-local detection

Instrument detections must include time ranges.

Example:

```json
{
  "label": "rhodes_electric_piano",
  "confidence": 0.91,
  "start_sec": 31.42,
  "end_sec": 62.03,
  "attributes": {
    "stereo": 0.88,
    "chorused": 0.72,
    "dark_timbre": 0.64
  }
}
```

A piano that only enters in the hook must not be treated as present throughout the entire song.

## 8.4 Confidence behavior

Never fabricate certainty.

Production inference should use calibrated, class-specific thresholds rather than one universal score.

Example UI behavior:

- High confidence: `Rhodes electric piano`
- Medium confidence: `Electric piano — likely Rhodes`
- Lower confidence: `Keyboard instrument`
- Insufficient confidence: `Unknown tonal layer`

The exact thresholds are determined by calibration datasets and may differ by class.

## 8.5 Multi-label behavior

The engine must support simultaneous layered detections.

Example:

- Rhodes
- string sample
- synth bell

may all contribute to the same melodic section.

## 8.6 Instrument characteristics

Every useful instrument detection should be augmented with measurable characteristics when possible.

Example 808 profile:

- estimated fundamental/pitch track
- strongest low-frequency region
- decay length
- harmonic content
- distortion/saturation probability
- stereo width
- onset alignment relative to kick
- phase/coherence relationship with kick
- limiter contribution

Example snare profile:

- transient strength
- body frequency region
- noise/air region
- likely clap layer
- section consistency
- clipping/saturation probability

This information feeds repair decisions.

---

# 9. Beat-Specific Intelligence

## 9.1 Kick/808 subsystem

This is a dedicated first-class subsystem.

Detect:

- overlapping fundamentals
- masking between kick and 808
- kick attack disappearing under 808 sustain
- 808 consuming limiter headroom
- excessive sub decay
- phase cancellation
- timing relationship
- excessive low-frequency stereo
- inaudibility on small speakers
- insufficient upper harmonics
- accidental sub distortion
- intentional clipped/distorted 808 character
- unstable note-to-note bass level

Possible interventions:

- dynamic frequency-selective ducking
- sidechain-controlled EQ
- phase/time alignment only when confidence and musical safety are high
- transient enhancement of kick
- controlled 808 envelope shaping
- harmonic generation for small-speaker translation
- stereo-to-mono low-frequency correction
- clipper strategy adjustment
- limiter release/architecture change

The engine must not automatically "fix" stylistic 808 distortion simply because it is nonlinear.

## 9.2 Intentional dirt vs defect

The analyzer must estimate whether artifacts are intentional character.

Examples to preserve unless clearly harmful:

- vinyl noise
- sample grit
- SP/MPC-like crunch
- clipped drums
- intentional saturation
- filtered/narrow samples
- tape instability
- distorted bass
- aggressive drum-bus compression
- lo-fi bandwidth restriction

Repair decision must consider context, repetition, correlation with transients, section consistency, genre/style, and user profile.

---

# 10. Source Separation and Source-Aware Processing

## 10.1 Principle

Source separation is a tool, not an automatic requirement.

The engine should choose among three modes:

### Mode 0 — stereo mastering

Use only the original mix when no source-specific intervention is needed.

### Mode 1 — source-guided stereo processing

Run separation/mask estimation to identify sources, but apply correction to the **original mix** using masks, sidechains, or time-frequency control signals.

This is preferred when it can fix the problem without audible reconstruction artifacts.

### Mode 2 — stem reconstruction

Process separated stems and reconstruct the mix only when:

- the defect cannot be solved acceptably in the original mix,
- separation confidence is sufficiently high,
- estimated artifact cost is below the expected repair benefit.

## 10.2 Quality gates

Before using reconstructed stems, measure/estimate:

- leakage
- musical-noise artifacts
- transient damage
- phase changes
- high-frequency smearing
- vocal/instrument residue
- reconstruction null/residual energy
- model confidence

If quality is insufficient, fall back to source-guided stereo processing or plain stereo mastering.

## 10.3 Separation model policy

Model implementation and model weights are licensed separately and must both be audited.

No model is shippable merely because its source code repository is permissively licensed.

Every production model requires a manifest containing:

- model name
- model version
- model hash
- architecture/source
- weight provenance
- code license
- weight license
- redistribution rights
- commercial-use status
- attribution requirements
- supported execution providers
- expected input sample rate
- stem taxonomy
- benchmark record

Research baselines such as Demucs may be evaluated, but archived/unmaintained projects or ambiguously licensed weights must not silently become production dependencies.

---

# 11. Mix Health Model

The application displays multiple dimensions instead of one fake universal quality score.

Initial dimensions:

- Tonal Balance
- Dynamics
- Low End
- Transients
- Stereo / Phase
- Clarity
- Translation
- Loudness

Each score is a model-derived assessment with explanatory evidence.

The UI should label these as analysis estimates, not objective artistic grades.

Each dimension should expose internally:

- score 0–100
- confidence
- evidence list
- affected sections
- source/instrument relationships
- recommended action category

---

# 12. Repair Engine

## 12.1 Repair philosophy

Repair is conditional.

A good mix may require almost no corrective processing.

Every repair operation requires:

- evidence
- expected benefit
- confidence
- risk estimate
- bounded parameter range
- post-process validation

If post-process validation predicts degradation, revert that operation.

## 12.2 Repair capabilities

Target capabilities:

- DC removal
- channel balance correction
- declipping when confidence is high
- denoise when clearly necessary
- artifact reduction
- de-reverberation only when justified
- resonance suppression
- harshness control
- de-essing
- tonal imbalance correction
- low-end cleanup
- phase/mono compatibility repair
- transient restoration
- transient control
- section consistency correction
- source-aware masking correction
- stereo-field repair

## 12.3 Microscopic reconstruction boundary

Allowed:

- restoring damaged transients
- reconstructing small clipped regions
- filling short localized artifacts
- neural restoration where output remains the same performance

Not allowed by default:

- changing notes
- rewriting arrangement
- replacing instruments with new performances
- changing lyric content
- changing timing creatively
- generative re-composition

---

# 13. Mastering DSP Engine

## 13.1 Graph-based design

The mastering chain must be represented as a processing graph, not hard-coded as one fixed series.

Candidate node types include:

- gain staging
- DC/high-pass cleanup
- static EQ
- minimum-phase EQ
- linear/mixed-phase EQ where justified
- dynamic EQ
- resonance suppression
- broadband compression
- multiband compression
- upward/downward dynamics where needed
- transient shaping
- saturation
- harmonic enhancement
- M/S processing
- frequency-dependent stereo width
- low-frequency mono management
- clipper
- limiter
- true-peak safety
- dither at final integer export only

## 13.2 Automatic graph planning

The decision engine selects:

- which nodes are needed
- processing order
- parameter ranges
- section-aware automation if needed
- whether an operation is global, band-limited, or source-guided

## 13.3 Stereo behavior

Required:

- sub frequencies kept appropriately centered when necessary
- width analyzed per frequency band
- phase correlation constraints
- vocal/central-source preservation
- avoid widening already unstable material
- width changes may vary by section/instrument

## 13.4 Clipping/limiting strategy

The engine chooses among:

- limiter-only
- clipper → limiter
- staged clipping → limiter
- transient preconditioning → clipping → limiting

Selection depends on:

- source transient profile
- genre/style
- bass behavior
- desired loudness
- distortion tolerance
- oversampled audition results
- predicted translation

## 13.5 Oversampling

Nonlinear processing must support appropriate oversampling during final render.

Preview may use reduced settings, but final render should use high-quality oversampling/resampling appropriate to the process.

---

# 14. Candidate Generation Engine

## 14.1 Candidate A — Recommended

Optimize a multi-objective score across:

- tonal balance
- dynamics
- transient preservation
- low-end control
- stereo/phase
- distortion/artifact risk
- translation
- loudness
- genre/style fit
- user preference profile
- reference influence if enabled

## 14.2 Candidate B — Alternative

Always provide a preservation-biased alternative.

It should usually:

- make fewer aggressive repairs
- preserve more of the original macro-dynamics
- use less source reconstruction
- provide an honest safety comparison

It may still be loud and polished; "conservative" describes intervention risk, not necessarily quietness.

## 14.3 Search strategy

Early versions may use:

1. deterministic heuristics to propose graphs
2. bounded parameter search
3. objective/perceptual proxy scoring
4. reject unsafe candidates
5. render two finalists

Later versions should add a learned ranking model trained from pairwise human preferences and the user's own selections.

## 14.4 Preview vs final

### Preview path

- faster analysis reuse
- lower-cost separation model/mode when possible
- reduced oversampling
- cache-heavy
- optimized for audition latency

### Final path

- maximum-quality approved model
- full-track analysis consistency
- high oversampling
- high-quality resampling
- offline lookahead
- final true-peak verification
- export-specific quantization/dither

The selected final master must be revalidated after maximum-quality rendering.

---

# 15. Loudness-Matched A/B

Loudness matching is ON by default.

Requirements:

- switching must preserve playhead position
- audition gain is temporary and never alters exported files
- match using perceptually meaningful loudness rather than peak normalization
- switching gain should avoid audible jumps/clicks
- UI must clearly indicate audition matching is active
- allow bypass only if user wants to compare delivered loudness

Acceptance target: matched audition variants should remain within a tightly controlled loudness tolerance so louder output does not automatically win subjective comparison.

---

# 16. Reference Mastering

## 16.1 Optional reference

Reference use is fully bypassable.

When supplied, analyze the reference independently and derive a reusable `ReferenceProfile`.

## 16.2 Reference dimensions

At minimum:

- broad spectral balance
- low-frequency distribution
- macro dynamics
- transient character
- crest/PLR behavior
- loudness/density
- stereo-width curve
- saturation/distortion character
- brightness/air
- section contrast

## 16.3 Influence

User control:

- Loose
- Medium
- Close

Even `Close` must preserve the source track's identity and must not blindly force exact spectral matching.

## 16.4 Saved profiles

Reference profiles should be savable without requiring the original audio file.

Store derived descriptors, versioning, and provenance metadata. Do not store copyrighted reference audio inside the profile unless explicitly requested and legally permitted.

---

# 17. Personal Sound Profiles and Preference Learning

## 17.1 Multiple profiles

Support named profiles such as:

- Boom Bap
- Modern Trap
- Warm Sample Beats
- Beat Battle
- Clean Client Master

## 17.2 Profile creation

Profiles may be built from:

- multiple reference tracks
- chosen candidate history
- accepted/rejected revisions
- natural-language descriptions

## 17.3 Preference learning

The system learns from:

- A vs B selections
- rejected masters
- accepted revisions
- frequently repeated instructions
- preferred loudness/dynamics trade-offs

Preference data must be separable from global model training. Raw user audio must not be used for global model training without explicit consent.

---

# 18. Natural-Language Mastering Agent

## 18.1 Role

Natural language controls an already bounded mastering system; it does not get direct unrestricted access to DSP parameter memory.

## 18.2 Command pipeline

1. Parse user intent.
2. Identify target source/instrument/time region.
3. Resolve constraints.
4. Convert to a typed `RevisionIntent`.
5. Validate against safe operations.
6. Generate/update mastering graph.
7. Render preview.
8. Run regression checks.
9. Present what changed.

## 18.3 Typed intent examples

```json
{
  "operation": "increase_punch",
  "target": "kick",
  "amount": "moderate",
  "constraints": ["preserve_808_level", "preserve_snare"]
}
```

```json
{
  "operation": "reduce_harshness",
  "target": "hi_hat",
  "amount": "slight",
  "time_scope": "global"
}
```

## 18.4 Safety constraints

Natural language must not permit:

- NaN/unstable parameter values
- unbounded gain
- out-of-range filter Q
- unsafe feedback
- excessive true peak
- impossible graph states

All generated decisions pass through deterministic validation.

---

# 19. Playback Translation Engine

## 19.1 Target playback classes

At minimum:

- studio monitors
- headphones
- earbuds
- phone speaker
- laptop speaker
- small Bluetooth speaker
- car
- mono playback
- large PA/club-style system

## 19.2 Method

Use generic, legally distributable playback-class models rather than pretending to emulate a specific branded device.

Translation analysis can combine:

- transfer-function filtering
- bandwidth limitation
- stereo collapse behavior
- bass audibility estimation
- nonlinear small-speaker constraints
- instrument audibility/importance models

## 19.3 Instrument-aware translation

Example:

If an 808's fundamental is below a phone speaker's useful range and it contains little upper harmonic information, the engine should recognize that bass identity may disappear rather than simply reporting "low bass."

Potential solution: controlled harmonic enhancement while preserving the fundamental on full-range systems.

---

# 20. Album / Beat-Tape Mode

## 20.1 Batch workflow

1. Ingest all tracks.
2. Analyze every track individually.
3. Analyze collection-level statistics.
4. Determine cohesion targets.
5. Master each track within those collection constraints.
6. Re-evaluate the sequence as a whole.
7. Export project with track-level and album-level reports.

## 20.2 Cohesion dimensions

- perceived loudness
- bass weight
- brightness
- stereo image
- transient density
- saturation
- dynamic character
- overall tonal identity

Do not flatten intentional contrast between songs.

---

# 21. Project and History Model

Every source becomes a project.

Suggested manifest:

```json
{
  "project_version": 1,
  "project_id": "uuid",
  "source": {
    "path": "...",
    "sha256": "...",
    "sample_rate": 48000,
    "channels": 2
  },
  "analysis_version": "...",
  "model_versions": {},
  "analysis_report": "analysis/report.json",
  "references": [],
  "candidate_graphs": [],
  "revisions": [],
  "selected_revision": null,
  "exports": []
}
```

History is append-oriented. Old revisions should not silently change when models are updated.

Re-opening an old project must show which model/DSP versions originally created it.

---

# 22. Software Architecture

## 22.1 Core principle

Mastering logic must not depend on the UI framework, plugin wrapper, or cloud transport.

Recommended layers:

```text
                ┌──────────────────────────┐
                │      Desktop UI/App      │
                └────────────┬─────────────┘
                             │
                ┌────────────▼─────────────┐
                │   Application Services   │
                │ projects/jobs/history    │
                └────────────┬─────────────┘
                             │
        ┌────────────────────▼────────────────────┐
        │             Mastering Core              │
        │ analysis • planning • DSP • validation  │
        └───────────┬───────────────────┬─────────┘
                    │                   │
          ┌─────────▼────────┐ ┌────────▼────────┐
          │ Inference Layer │ │ Audio I/O/Codec │
          └─────────┬────────┘ └─────────────────┘
                    │
       ┌────────────┼────────────────────┐
       │            │                    │
   CPU runtime   GPU runtime        Cloud worker
```

## 22.2 Recommended languages

### Production runtime

- C++20/23 for audio/DSP/core orchestration.
- CMake for builds.
- ONNX Runtime C/C++ API for deployable inference where models support ONNX.

### ML research/training

- Python for training, benchmarking, dataset preparation, PyTorch experiments, model export, and evaluation.
- Python is not required in the final desktop runtime once production models are exported.

### Web

- TypeScript/React frontend.
- WebAssembly for compatible deterministic DSP/core components.
- ONNX Runtime Web/WebGPU for compatible models.
- Cloud fallback for models too large or unsupported in-browser.

## 22.3 Desktop/plugin framework decision

The mastering core must be framework-independent.

**Initial recommended host:** JUCE 8 for the Windows application because it has mature desktop/audio/plugin support across Windows, macOS, and Linux and can later generate VST3/Standalone targets from the same host layer.

However, JUCE licensing must be reviewed before public/commercial distribution. Keep the host wrapper thin enough that iPlug2/direct VST3 hosting remains a viable migration path.

Current VST3 SDK versions 3.8+ use the MIT license, so VST3 itself should not be treated as the licensing blocker.

## 22.4 Inference abstraction

Define an interface similar to:

```cpp
class IInferenceSession {
public:
  virtual TensorMap run(const TensorMap& inputs) = 0;
  virtual ModelInfo info() const = 0;
  virtual ~IInferenceSession() = default;
};
```

Execution providers are runtime-selected.

Target order:

1. CUDA on supported NVIDIA hardware.
2. Cross-vendor native WebGPU where stable/supported.
3. Other supported platform acceleration as validated.
4. CPU fallback.
5. Cloud when configured/needed.

ONNX Runtime provides a common execution-provider interface and currently documents CPU, CUDA, WebGPU, OpenVINO, DirectML and other providers. Provider choice must be benchmarked per model instead of assumed.

## 22.5 Worker isolation

Heavy inference should run in a separate worker process where practical.

Benefits:

- crashes do not kill the UI/project
- GPU memory can be released by terminating a worker
- model versions can be isolated
- cloud/local job APIs can share concepts
- VST3 can keep realtime audio code separate from expensive offline ML

The desktop app communicates with the worker through versioned IPC messages.

---

# 23. Proposed Repository Layout

```text
/
├─ CMakeLists.txt
├─ cmake/
├─ apps/
│  ├─ desktop/
│  └─ cli/
├─ core/
│  ├─ audio/
│  ├─ analysis/
│  ├─ instruments/
│  ├─ separation/
│  ├─ repair/
│  ├─ dsp/
│  ├─ mastering/
│  ├─ decision/
│  ├─ reference/
│  ├─ translation/
│  ├─ project/
│  ├─ export/
│  └─ inference/
├─ workers/
│  └─ inference-worker/
├─ ml/
│  ├─ datasets/
│  ├─ training/
│  ├─ export/
│  ├─ evaluation/
│  └─ notebooks/
├─ models/
│  ├─ manifests/
│  └─ README.md
├─ web/
├─ tests/
│  ├─ unit/
│  ├─ integration/
│  ├─ conformance/
│  ├─ audio-golden/
│  ├─ model-eval/
│  └─ listening/
├─ docs/
│  ├─ architecture/
│  ├─ dsp/
│  ├─ models/
│  ├─ qa/
│  └─ decisions/
├─ tools/
├─ third_party/
└─ .github/workflows/
```

Large model weights should not be casually committed to normal Git history. Use a controlled model-download/cache mechanism, release assets, artifact storage, or Git LFS only after license/distribution review.

---

# 24. Core Data Contracts

Define stable versioned structures early.

## 24.1 `AnalysisReport`

Contains:

- technical metadata
- loudness
- dynamics
- spectral analysis
- stereo/phase
- structure/sections
- instrument events
- defects
- source interactions
- translation results
- mix-health dimensions

## 24.2 `InstrumentEvent`

```cpp
struct InstrumentEvent {
  InstrumentId id;
  InstrumentFamily family;
  double confidence;
  TimeRange range;
  AttributeMap attributes;
  std::vector<FrequencyRegion> activeRegions;
};
```

## 24.3 `Diagnosis`

```cpp
struct Diagnosis {
  DiagnosisType type;
  Severity severity;
  double confidence;
  TimeRange range;
  std::vector<InstrumentId> sources;
  Evidence evidence;
  std::vector<RepairProposal> proposals;
};
```

## 24.4 `MasteringPlan`

Stores a complete renderable graph plus constraints and provenance.

## 24.5 `RevisionIntent`

Typed output of natural-language commands.

## 24.6 Versioning rule

All serialized data contracts include schema version numbers. Migration code is required when incompatible fields change.

---

# 25. Model Registry and Governance

Every model must have a machine-readable manifest.

Example:

```yaml
id: instrument-detector-v1
version: 1.0.0
sha256: ...
format: onnx
input_sample_rate: 48000
window_seconds: 5.0
license:
  code: ...
  weights: ...
  commercial_use: true
  redistribution: true
providers:
  - cpu
  - cuda
  - webgpu
metrics:
  macro_f1: ...
  calibration_ece: ...
```

CI must reject production model manifests missing licensing/provenance fields.

Models must be replaceable without changing business logic.

---

# 26. Licensing Policy

This project may eventually be publicly distributed. Dependency and model licensing therefore matters from day one.

Rules:

1. Maintain `THIRD_PARTY_NOTICES.md`.
2. Maintain `MODEL_LICENSES.md`.
3. Track code license separately from weight license.
4. Avoid assuming research models are commercially redistributable.
5. Do not ship NC-only models in a commercial build.
6. Treat AGPL libraries/models as architecture-affecting decisions.
7. Build FFmpeg only with an explicitly reviewed feature/license configuration if it is used.
8. Do not enable GPL FFmpeg components accidentally in a build intended for a different licensing model.
9. Pin third-party versions/hashes.

Known design implications:

- VST3 SDK 3.8+ is MIT licensed.
- JUCE 8 has its own licensing terms and must be reviewed before distribution.
- iPlug2 is liberally/zlib-like licensed but does not currently provide the same mature Linux story as JUCE; keep it as a potential host alternative.
- FFmpeg is generally LGPL 2.1+ but becomes GPL when certain GPL components are enabled; build configuration matters.
- Essentia and many of its provided models have non-commercial/open-source licensing constraints; do not make them a production dependency without an explicit licensing decision.
- Demucs source is MIT, but model-weight rights/provenance still require verification; the original Meta repository is archived.

---

# 27. Performance and Resource Budgets

Targets are engineering budgets, not promises to users.

## 27.1 Desktop responsiveness

- UI thread must never run model inference or long DSP renders.
- Playback must remain glitch-free while analysis is occurring.
- Job cancellation must be cooperative and reliable.
- Project can recover from worker crash without losing source/history.

## 27.2 Memory

- Stream long files rather than loading them entirely.
- Reuse tensor buffers.
- Cache only reusable intermediate data.
- Evict stem/model caches based on size and recency.
- Allow user-controlled cache location/limit.

## 27.3 GPU

- Detect available VRAM.
- Select model precision/tile size based on capacity.
- Gracefully retry with smaller chunks or CPU/cloud if allocation fails.
- Never crash the project because CUDA is missing.

## 27.4 Determinism

Traditional DSP render paths should be deterministic for the same graph, source, and build where possible.

Model inference may have backend-specific tolerance but must be regression-tested.

---

# 28. Security / Privacy

Required:

- no arbitrary executable model downloads
- model files validated by manifest/hash
- TLS for cloud jobs
- signed/update-verified production binaries
- cloud job authentication when accounts exist
- explicit project/cache deletion
- local-only mode
- no global training on user audio without opt-in
- safe filename/path handling
- archive extraction protections

If cloud processing is used, job lifecycle should support automatic deletion/retention policy rather than indefinite raw-audio storage.

---

# 29. Testing Strategy

## 29.1 Unit tests

DSP units require tests for:

- silence
- impulse
- sine
- sweeps
- pink/white noise
- DC
- clipped signals
- denormals
- NaN/Inf rejection
- mono/stereo edge cases
- sample-rate variations

## 29.2 Loudness conformance

Validate BS.1770 measurements against trusted reference vectors/tools.

Track:

- integrated loudness error
- true-peak error
- gating behavior
- sample-rate behavior

## 29.3 DSP golden tests

Store tiny legally safe synthetic/owned fixtures with known expected outputs.

Tests should verify:

- frequency response
- latency
- gain
- phase
- oversampling
- limiter ceiling
- clipping behavior
- deterministic output hashes/tolerances

## 29.4 Model evaluation

Instrument detector metrics:

- macro/micro F1
- per-class precision/recall
- hierarchical accuracy
- multilabel average precision
- time-local event accuracy
- calibration/error-confidence metrics

The product should prefer honest fallback labels over incorrect high-confidence exact labels.

## 29.5 Separation evaluation

Use:

- SDR/SI-SDR where appropriate
- leakage metrics
- transient preservation
- perceptual artifact tests
- reconstruction/null metrics
- human listening

Numerical separation score alone is insufficient for mastering use.

## 29.6 Mastering evaluation

Maintain a legally owned/cleared mastering corpus spanning:

- boom bap
- trap
- sample beats
- lo-fi
- R&B
- pop
- rock
- electronic
- vocal songs
- intentionally clipped masters
- weak mixes
- already-good mixes

For each source, maintain:

- original mix
- expert notes
- known problems
- optional professional reference master
- expected no-touch regions

## 29.7 Human listening tests

Use pairwise blind tests and, for research milestones, MUSHRA-like controlled comparisons where appropriate.

Measure:

- preference vs unmastered
- preference vs previous engine version
- damage rate on already-good mixes
- preference between A/B
- artifact detection
- translation preference

The release gate should emphasize **regression avoidance** as much as average improvement.

## 29.8 Competitor baseline tests

Where legally and practically possible, compare on user-owned/cleared audio against current automatic-mastering workflows using blind playback and loudness matching.

Do not copy competitor output or reverse engineer proprietary internals.

---

# 30. CI/CD Plan

## 30.1 Pull request CI

Windows first:

- configure CMake
- build Debug/Release core
- unit tests
- static analysis
- formatting checks
- model-manifest validation
- license/provenance checks
- deterministic DSP tests

## 30.2 Tools

Recommended:

- clang-format
- clang-tidy
- Catch2 or GoogleTest
- sanitizers on supported CI targets
- Python: pytest + ruff + typing checks

## 30.3 Audio regression CI

Keep large audio fixtures out of ordinary PR jobs.

Run a separate scheduled/manual audio-quality workflow that:

- downloads approved golden fixtures
- runs full renders
- computes metrics
- publishes HTML/JSON report
- stores artifacts for A/B review

No model or DSP change is considered complete until audio regression passes.

---

# 31. Implementation Roadmap

## Phase 0 — Repository, legal, and architecture foundation

### Deliverables

- CMake project skeleton.
- `core` static library.
- Windows CLI proof of concept.
- desktop shell skeleton.
- test framework.
- GitHub Actions Windows workflow.
- coding conventions.
- architecture decision records.
- third-party dependency manifest.
- model-license manifest format.
- benchmark harness.

### Mandatory technical spikes

1. JUCE 8 standalone shell vs alternative host feasibility.
2. ONNX Runtime CPU + CUDA inference from C++.
3. ONNX Runtime native WebGPU evaluation.
4. audio decoder strategy and FFmpeg license-safe build evaluation.
5. 60-minute streaming decode/render test.
6. worker-process IPC test.

### Exit criteria

- clean clone builds on Windows.
- unit tests run in CI.
- CLI loads and losslessly re-renders supported baseline WAV/FLAC files.
- selected dependency strategy is recorded.

---

## Phase 1 — Audio foundation and standards-compliant metering

### Build

- decoder/encoder abstraction
- streaming audio reader
- float processing buffers
- resampler abstraction
- waveform peak cache
- transport/playback engine
- BS.1770 loudness meter
- true-peak meter
- spectral analyzer
- stereo/phase analyzer
- export pipeline

### Tests

- sample-rate matrix
- long-file streaming
- loudness conformance
- true-peak conformance
- format round trips

### Exit criteria

The app can load, play, analyze, display waveform, report technical metrics, and export a transparent pass-through file without corruption.

---

## Phase 2 — Deterministic mastering baseline

Build mastering nodes:

- gain
- EQ
- dynamic EQ
- compressor
- multiband dynamics
- transient shaper
- saturation
- stereo/M-S processor
- clipper
- true-peak limiter
- dither

Build graph serialization and offline renderer.

Create first heuristic mastering planner.

### Exit criteria

- two deterministic candidate graphs can be generated from basic analysis.
- no AI/stems required yet.
- A/B is loudness matched.
- rendered masters obey peak/export constraints.

This phase establishes an audio-quality baseline that later AI features must beat.

---

## Phase 3 — Structural and perceptual analysis

Build:

- tempo/beat analysis
- onset/transient analysis
- section segmentation
- tonal balance features
- macro-dynamics features
- bandwise stereo analysis
- harshness/resonance candidates
- clipping/saturation detector
- intentional-vs-accidental defect feature set
- Mix Health v1

### Exit criteria

Analyzer produces a complete versioned `AnalysisReport` and plain-English diagnosis without instrument-specific AI yet.

---

## Phase 4 — Two-master UX and project history

Build the actual product workflow:

- drag/drop
- waveform
- analysis state
- Master A/B cards
- recommendation explanation
- synchronized Original/A/B
- loudness match
- project persistence
- revision tree storage
- export recipes

### Exit criteria

A non-technical user can master a song end-to-end without seeing engineering controls.

---

## Phase 5 — Source separation research and source-guided processing

### Research

Benchmark candidate models for:

- quality
- speed
- VRAM
- CPU fallback
- exportability
- ONNX compatibility
- model/code license

### Implement

- separation interface
- stem/mask cache
- artifact estimator
- Mode 0/1/2 selection
- source-guided dynamic masking
- reconstruction safety checks

### Exit criteria

The engine can use source estimates without forcing reconstructed stems into every master.

---

## Phase 6 — Instrument-aware analysis v1

### Build

- hierarchical instrument taxonomy
- multilabel classifier interface
- time-local event detector
- confidence calibration
- unknown/fallback behavior
- instrument characteristic extractors
- UI summary

### Initial priority classes

Prioritize beat-relevant classes first:

1. kick
2. snare
3. clap/rim
4. closed/open hat
5. cymbals
6. 808
7. synth bass
8. electric bass
9. piano
10. Rhodes/Wurlitzer family
11. guitar acoustic/electric/distorted
12. strings
13. brass
14. sax/flute/woodwinds
15. synth lead/pad/pluck
16. lead vocal
17. backing vocal/ad-lib
18. percussion families

### Exit criteria

The system can state specific instruments with calibrated confidence and fall back to family/unknown when exact labeling is unsafe.

---

## Phase 7 — Kick/808 and instrument interaction engine

Build:

- kick onset/pitch/body tracker
- 808 pitch/envelope tracker
- phase/coherence analysis
- masking analysis
- limiter contribution estimate
- small-speaker bass survival estimate
- instrument-pair interaction graph

Implement bounded repairs.

### Exit criteria

Blind tests demonstrate that source-aware kick/808 repair improves targeted problem mixes without damaging already-good mixes at an unacceptable rate.

---

## Phase 8 — Advanced mix repair / restoration

Build high-confidence-only:

- declipping
- transient reconstruction
- denoise
- localized artifact reduction
- optional dereverb
- neural repair provider interface

Every repair must include before/after validation and automatic fallback.

### Exit criteria

Repair operations activate only when predicted improvement exceeds artifact risk.

---

## Phase 9 — Mastering decision engine v2

Replace simple heuristic candidate selection with a hybrid system:

- deterministic safety rules
- candidate graph generator
- bounded optimizer
- perceptual proxy scorer
- translation scorer
- artifact penalties
- learned ranker when sufficient human data exists

Maintain explainability metadata for every recommendation.

### Exit criteria

Candidate A/B differ meaningfully, the recommended candidate wins blind comparisons against the Phase 2 baseline at a statistically useful rate, and damage rate on already-good mixes is reduced.

---

## Phase 10 — Reference mastering and My Sound profiles

Build:

- reference analysis
- profile vector format
- Loose/Medium/Close influence
- saved reference profiles
- multi-track personal profiles
- versioned preference model

### Exit criteria

Profiles influence mastering character without blindly matching a reference spectrum or destroying track identity.

---

## Phase 11 — Natural-language revision engine

Build:

- intent schema
- command parser
- source/instrument resolver
- constraints
- mastering-plan editor
- revision explanation
- cloud LLM provider abstraction
- local fallback for common commands

### Exit criteria

Core commands reliably alter the intended property/source while obeying explicit constraints such as `don't touch the bass`.

---

## Phase 12 — Translation engine

Build generic playback-class simulations and instrument-survival analysis.

Use translation both for:

- user-facing analysis
- candidate scoring

### Exit criteria

Translation warnings correspond to reproducible weaknesses and candidate scoring improves small-system playback without degrading full-range playback.

---

## Phase 13 — Preference learning

Build:

- local preference event store
- A/B selection learning
- revision preference features
- per-profile preference vector
- reset/export/import

### Exit criteria

Preference adaptation is measurable, reversible, and never silently rewrites old projects.

---

## Phase 14 — Album / batch mastering

Build:

- batch ingest
- collection analysis
- album cohesion target
- track-to-track loudness/tonal planning
- batch queue
- sequence audition
- batch export

### Exit criteria

The collection sounds cohesive while retaining intentional track contrast.

---

## Phase 15 — Windows standalone hardening

### Product work

- installer
- crash reporting with privacy controls
- updater
- settings
- cache management
- model manager
- local/cloud settings
- accessibility
- keyboard controls
- high-DPI UI
- Windows audio-device robustness

### QA

- clean-machine install
- no-GPU machine
- NVIDIA GPU matrix
- low-VRAM fallback
- long songs
- weird metadata/filenames
- corrupted media
- cancellation/restart
- interrupted cloud job recovery

### Exit criteria for Windows 1.0

- reliable end-to-end workflow
- mastering quality clears listening benchmark
- no known data-loss issue
- correct loudness/true-peak reporting
- model/dependency licenses documented
- project backward compatibility policy established

---

## Phase 16 — VST3 / ARA-capable architecture

### VST3 behavior

Support both:

- real-time-safe normal DSP path
- offline advanced analysis/repair path

Real-time audio thread must never perform:

- network calls
- dynamic model loading
- unbounded allocation
- stem separation
- large ML inference

Advanced mastering jobs should be rendered through an offline workflow.

### ARA

Design the analysis/project API so future ARA-style whole-song access can be added without rewriting the mastering engine.

### Exit criteria

- validator passes
- common DAW smoke tests pass
- project recall works
- plugin never blocks the realtime audio thread with ML jobs

---

## Phase 17 — Web application

Build:

- React/TypeScript interface
- browser ingest
- waveform
- WebAssembly DSP subset
- ONNX Runtime Web/WebGPU model path
- cloud job fallback
- project synchronization strategy

Do not assume every desktop model belongs in-browser. Capability negotiation chooses local web vs cloud.

---

## Phase 18 — macOS and Linux

Port/test core first, then app host.

macOS:

- Apple Silicon
- Intel only if still a supported requirement
- code signing/notarization
- CoreAudio

Linux:

- choose supported distributions
- audio backend matrix
- package formats
- GPU provider availability

---

# 32. Milestone Versioning Proposal

- **0.0.1** repository/bootstrap
- **0.1** audio I/O + metering
- **0.2** deterministic automatic mastering
- **0.3** structural/perceptual analysis
- **0.4** complete two-master desktop workflow
- **0.5** source separation/source-guided repair
- **0.6** instrument-aware analysis
- **0.7** kick/808 + interaction-aware repair
- **0.8** reference profiles + natural language + translation
- **0.9** album mode + preference learning + release hardening
- **1.0** production-quality Windows standalone
- **1.1** VST3
- **1.2** web
- **1.3** macOS
- **1.4** Linux

Version numbers can change; exit criteria matter more than labels.

---

# 33. Definition of Done for Core Features

A feature is not done because it produces output.

It is done only when it has:

- implementation
- unit tests
- integration tests
- audio regression tests where applicable
- error handling
- cancellation behavior
- serialization/versioning if persistent
- logging/diagnostics
- performance benchmark
- license/provenance review for dependencies/models
- documentation
- listening validation when it changes sound

---

# 34. Critical Risk Register

## Risk 1 — "AI mastering" sounds worse than a simple good limiter chain

Mitigation:

- establish deterministic baseline first
- every AI feature must beat the baseline
- preservation candidate always available
- automated post-process regression checks

## Risk 2 — stem artifacts damage the master

Mitigation:

- source-guided stereo mode
- artifact scoring
- reconstruction quality gate
- automatic fallback

## Risk 3 — instrument classifier hallucinates exact instruments

Mitigation:

- hierarchical labels
- calibrated confidence
- explicit unknown class
- UI wording based on confidence

## Risk 4 — excessive loudness destroys beat transients

Mitigation:

- multi-objective score
- transient-survival metric
- clipper/limiter search
- loudness-matched listening tests

## Risk 5 — training/model licenses block release

Mitigation:

- model registry/license gates from Phase 0
- keep models replaceable
- never assume code license covers weights

## Risk 6 — cloud costs become uncontrolled

Mitigation:

- local-first provider routing
- preview/full-render distinction
- cache analysis/stems
- cost-aware job scheduler

## Risk 7 — web forces architectural rewrite

Mitigation:

- platform-independent core contracts
- ONNX model contracts
- WASM-compatible deterministic DSP where practical
- cloud fallback by design

## Risk 8 — preference learning overfits one sound

Mitigation:

- separate named profiles
- bounded influence
- reset controls
- preserve base model recommendation

---

# 35. Initial Engineering Backlog

These are the first concrete repository tasks.

## Epic A — Bootstrap

- [ ] Add root CMake project.
- [ ] Add `core` library.
- [ ] Add CLI target.
- [ ] Add desktop target skeleton.
- [ ] Add tests target.
- [ ] Add Windows CI.
- [ ] Add formatting/lint configuration.
- [ ] Add dependency management strategy.
- [ ] Add `LICENSE` decision.
- [ ] Add `THIRD_PARTY_NOTICES.md`.
- [ ] Add `MODEL_LICENSES.md`.
- [ ] Add ADR template.

## Epic B — Audio I/O

- [ ] WAV reader/writer.
- [ ] FLAC reader/writer.
- [ ] AIFF reader/writer.
- [ ] compressed-codec strategy.
- [ ] streaming reader.
- [ ] float buffer abstraction.
- [ ] resampler interface.
- [ ] waveform cache.
- [ ] file integrity checks.

## Epic C — Metering

- [ ] sample peak.
- [ ] true peak.
- [ ] BS.1770 integrated loudness.
- [ ] short-term loudness.
- [ ] momentary loudness.
- [ ] LRA.
- [ ] crest/PLR metrics.
- [ ] conformance vectors.

## Epic D — Mastering DSP baseline

- [ ] EQ.
- [ ] dynamic EQ.
- [ ] compressor.
- [ ] multiband dynamics.
- [ ] transient shaper.
- [ ] saturation.
- [ ] M/S width.
- [ ] low-band mono control.
- [ ] clipper.
- [ ] true-peak limiter.
- [ ] dither.
- [ ] graph serializer.

## Epic E — Analysis

- [ ] spectral descriptors.
- [ ] stereo descriptors.
- [ ] transient/onset features.
- [ ] clipping/distortion features.
- [ ] section segmentation.
- [ ] defect inference.
- [ ] mix-health model.

## Epic F — ML runtime

- [ ] ONNX Runtime wrapper.
- [ ] CPU provider.
- [ ] CUDA provider.
- [ ] provider benchmarking.
- [ ] worker process.
- [ ] model manifest verification.
- [ ] model cache.

## Epic G — Instrument intelligence

- [ ] taxonomy schema.
- [ ] training/evaluation pipeline.
- [ ] multilabel model baseline.
- [ ] event segmentation.
- [ ] confidence calibration.
- [ ] hierarchical fallback.
- [ ] kick detector.
- [ ] 808 detector/tracker.
- [ ] instrument attribute extraction.

## Epic H — Separation

- [ ] model benchmark harness.
- [ ] license audit.
- [ ] stem interface.
- [ ] mask interface.
- [ ] artifact estimator.
- [ ] stem cache.
- [ ] source-guided repair API.

## Epic I — Decision engine

- [ ] diagnosis schema.
- [ ] mastering-plan schema.
- [ ] heuristic graph generator.
- [ ] candidate optimizer.
- [ ] safety validator.
- [ ] candidate ranker.
- [ ] explanation generator.

## Epic J — Desktop UX

- [ ] drop zone.
- [ ] waveform.
- [ ] transport.
- [ ] analysis status.
- [ ] plain-language findings.
- [ ] A/B cards.
- [ ] loudness-matched switching.
- [ ] natural-language revision field.
- [ ] project history.
- [ ] export recipes.

---

# 36. External Technical Baselines

Use these as reference points during implementation; pin actual dependency versions in the repository rather than relying on floating latest releases.

- ITU-R BS.1770-5: https://www.itu.int/rec/R-REC-BS.1770-5-202311-I
- EBU R 128: https://tech.ebu.ch/publications/r128
- ONNX Runtime execution providers: https://onnxruntime.ai/docs/execution-providers/
- ONNX Runtime WebGPU: https://onnxruntime.ai/docs/execution-providers/WebGPU-ExecutionProvider.html
- Steinberg VST3 licensing: https://steinbergmedia.github.io/vst3_dev_portal/pages/VST%2B3%2BLicensing/VST3%2BLicense
- JUCE licensing: https://juce.com/legal/juce-8-licence/
- FFmpeg legal/license notes: https://ffmpeg.org/legal.html

Research dependencies/models must be rechecked at the time they are selected because license terms, maintenance status, and model availability can change.

---

# 37. Final Product Acceptance Criteria

The product is successful when a user can:

1. Drop in a normal stereo mix.
2. Receive an understandable analysis without knowing mastering terminology.
3. See specific instrument detections when the engine can support them confidently.
4. Receive two distinct, polished masters.
5. Understand why one is recommended.
6. Switch instantly between Original/A/B at matched loudness.
7. Request a change in ordinary language.
8. Export a technically valid release master.
9. Reopen the project later with its history intact.
10. Repeat the process on an album/beat tape with collection-level cohesion.

And the engine must demonstrate through controlled listening tests that it improves weak/unfinished mixes while maintaining a very low damage rate on already-good mixes.

The central engineering principle is:

> **The system should understand what is making the sound before deciding how to change the sound.**

That includes identifying the actual instrument where possible—not merely labeling everything as bass, melody, or drums—and using that knowledge to make safer, more musical mastering decisions.