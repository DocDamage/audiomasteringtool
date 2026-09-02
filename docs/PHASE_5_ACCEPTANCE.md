# Phase 5 Acceptance — Source Separation Research + Source-Guided Mastering

## Status

Phase 5 is implementation-complete as a **safe, gated source-guidance system** once the runtime validation checklist in this document passes on Windows.

A gated feature is not incomplete merely because its safety gate remains closed. In particular:

- `automaticMode1Approved` must remain `false` until an explicit listening/damage policy passes.
- Mode 2 stem reconstruction must remain disabled in the normal desktop path.
- CUDA must remain optional; CPU execution is the required fallback.
- Exact instrument identification is Phase 6 work and is not a Phase 5 acceptance requirement.

Do not mark Phase 5 accepted by bypassing any of those gates.

## Scope

Phase 5 adds source-separation research, source-specific diagnostics, evidence-first source guidance, conservative source-guided stereo mastering, trusted model acquisition, worker-isolated ONNX inference, calibration rendering, and blind listening tooling.

The canonical imported stereo source remains immutable.

### Processing modes

**Mode 0 — Stereo mastering**

Normal mastering against the canonical stereo source. This is always available and is the required fallback.

**Mode 1 — Source-guided stereo processing**

Separated source estimates may determine where and how to modify the canonical stereo mix. Final audio is still rendered from the canonical stereo source. Automatic activation is calibration-gated.

**Mode 2 — Stem reconstruction**

Process separated stems and recombine them. Phase 5 includes evaluation and safety infrastructure, but desktop reconstruction remains intentionally disabled.

## Safety invariants

Phase 5 acceptance requires all of the following invariants to remain true:

1. The canonical stereo source is never modified in place.
2. Generic stereo analysis is never presented as proof that a particular source caused a problem.
3. Source-specific claims require measured separated-source evidence.
4. Heavy ONNX inference remains outside the desktop UI process in `amt_worker`.
5. Separation/model failure falls back to usable stereo mastering.
6. CPU execution remains supported.
7. Model identity, revision, artifact hash, runtime contract, stem taxonomy, licensing, provenance, and security review stay explicit.
8. Source-guided changes remain conservative and bounded.
9. Automatic Mode 1 stays controlled by explicit calibration evidence.
10. Mode 2 stays gated.

## Implemented Phase 5 systems

### Separation and source-guidance domain

Implemented under `src/separation/`:

- separation requests/results and stem-role contracts
- model manifests and registry loading
- source fingerprints and cache identity
- source-guidance policy and Mode 0/1/2 decisions
- source-specific issue inference
- diagnostic separation workflow
- source control envelopes
- bounded source-guided processing plans
- source-guided stereo executor/renderer
- reconstruction completeness and artifact-risk evaluation
- trusted model artifact installation
- worker-backed separation provider

### Source-specific diagnostics

Separated stem audio can conservatively support evidence for:

- excessive source level
- harshness
- muddiness
- excessive source width
- transient spikes
- bass/drum low-frequency masking

The system does not assign a stereo problem to a source unless the separated source provides supporting evidence.

### Desktop integration

The Phase 4 Windows shell is retained while its mastering call is redirected through the Phase 5 desktop adapter.

The adapter provides:

- packaged application/model discovery
- per-user model storage
- trusted model installation
- diagnostic separation
- source issue inference
- Mode 0/1 policy evaluation
- source-guided mastering when allowed
- effective-plan replacement after guidance replanning
- safe stereo fallback
- user-facing source diagnostic rationale
- machine-readable desktop diagnostic provenance

Desktop source diagnostics are persisted beside the project render directory as:

- `source-diagnostics-v1.json`
- `source-diagnostics-v1.txt`

### Production ONNX worker

`amt_worker` hosts production ONNX Runtime inference outside the UI process.

The worker includes:

- fixed float32 stereo input contract
- fixed tensor names and tensor shape validation
- chunked overlap-add processing
- sample-rate conversion to the model rate
- mono-to-stereo duplication
- unsupported channel-count rejection
- per-stem float WAV output
- non-finite sample checks
- bounded output/progress handling
- temporary/partial-artifact cleanup
- execution-provider control

### Current production model contract

Registry model: `htdemucs-onnx-fp16weights`

- pinned revision: `d54ed9eb60e258ea82131c6ee14578628816456a`
- pinned SHA-256: `d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a`
- pinned size: `165612636` bytes
- sample rate: `44100 Hz`
- input tensor: `mix`
- input shape: `[1, 2, 343980]`
- output tensor: `stems`
- output shape: `[1, 4, 2, 343980]`
- stems: `drums`, `bass`, `other`, `vocals`
- overlap: `85995` frames
- execution provider: CPU by default
- complete reconstruction: `false`
- automatic Mode 1 approval: `false`

### Trusted first-run model acquisition

The desktop application maps the model into the per-user model store under `%LOCALAPPDATA%\AudioMasteringTool\models` rather than expecting a writable `Program Files` installation.

The installer enforces a hard-coded trusted model catalog, exact model/revision/hash/byte-size identity, HTTPS-only retrieval, redirect controls, bounded streaming, sidecar download files, SHA verification before installation, atomic final rename, cancellation, and stale/invalid artifact cleanup.

### Calibration and blind listening

The calibration CLI can render the exact Mode 1 candidate that would be used if the final product gate were approved, without enabling automatic production use.

The calibration path produces:

- ordinary stereo Master A
- guided Master A when source evidence qualifies
- source diagnostics and issue evidence
- calibration manifest
- attenuation-only loudness-matched blind audition copies

Blind-listening tooling provides:

- randomized A/B listener bundles
- a separate private A/B key
- candidate-specific damage/side-effect flags
- locked-response decoding
- calibration corpus generation
- explicit policy evaluation
- registry approval only after every supplied policy check passes

The bundle generator rejects missing audition assets, positive audition gain, non-finite level metadata, identical A/B files, and attempts to reuse raw Master A files as blind audition assets.

## Automated test coverage

C++/CTest Phase 5 coverage includes:

- source-guided separation policy
- source fingerprint/cache identity
- reconstruction completeness
- reconstruction artifact evaluator
- source-guided processing plan
- source-control envelopes
- source-specific issue inference
- evidence-first source-guided workflow
- production model registry
- trusted model artifact installer
- source-guided stereo executor
- source-guided stereo renderer
- source-guided mastering integration
- effective mastering plan provenance

`tests/phase5_calibration_tooling_tests.py` adds pure filesystem/JSON coverage for:

- blind bundle creation
- no raw-master fallback
- private key separation
- listener response shape
- candidate-specific A/B ratings
- guided/stereo preference recovery
- missing blind audition fields
- missing candidate files
- malformed manifests
- positive audition gain rejection
- non-finite level metadata rejection
- identical candidate rejection
- registry approval refusal without a policy
- failed-policy refusal to modify the registry
- successful policy approval changing only the named model

The Python tooling test is registered with CTest when a Python 3 interpreter is available.

## Required Windows validation before final acceptance

These are runtime acceptance checks, not CI-cleanup work.

### 1. Configure and build

From a Windows Developer Command Prompt or equivalent MSVC environment:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release
```

The build must succeed for:

- `amt_worker`
- `amt_cli`
- `audiomasteringtool`
- Phase 5 libraries/tests
- ONNX Runtime linkage
- WinHTTP linkage
- required Windows SDK symbols

### 2. Run local tests

```powershell
ctest --preset windows-release
python scripts/validate_model_registry.py
```

All Phase 5 C++ tests and `phase5_calibration_tooling` must pass.

### 3. Validate real HTDemucs startup

With the per-user model artifact removed first:

1. Start a Phase 5 mastering operation.
2. Confirm the trusted first-run model download occurs.
3. Confirm the byte size and SHA-256 are verified.
4. Confirm the final artifact lands under `%LOCALAPPDATA%`.
5. Confirm `amt_worker.exe` launches.
6. Confirm ONNX Runtime DLL discovery succeeds.
7. Confirm a real source produces four valid stems.
8. Confirm stem sample rate, channel count, and frame geometry validate.
9. Confirm source-specific issues can reach the desktop rationale when evidence exists.
10. Confirm the desktop still renders canonical stereo while `automaticMode1Approved=false`.

### 4. Validate cache identity

Run the same source twice and confirm:

- first compatible request performs separation
- second compatible request uses the cache
- changing model identity invalidates the cache
- changing source content invalidates the cache
- the canonical source is untouched

### 5. Validate safe fallback

Force each of the following independently:

- missing worker
- missing registry
- failed model download
- corrupted model artifact
- source-separation failure
- cancellation

Expected behavior: normal stereo mastering remains available. Cancellation may cancel the requested job, but must not corrupt the canonical source or leave a partially installed trusted model as valid.

### 6. Validate desktop provenance persistence

For a project that runs source diagnostics:

- confirm source diagnostics appear in Master A rationale when supported by evidence
- confirm the mode/gating message is truthful
- confirm `source-diagnostics-v1.json` is written beside the render directory
- confirm `source-diagnostics-v1.txt` is written beside the render directory
- confirm reopening the project leaves the sidecar available for later UI restore work

### 7. Validate calibration workflow

Run:

```powershell
amt_cli calibrate-source-guidance <input> <output-directory> --registry <registry.json> --worker <amt_worker.exe> --model-root <model-root>
```

For a source that qualifies for a guided candidate, confirm:

- stereo and guided Master A renders exist
- blind audition copies exist
- both blind audition gains are finite and non-positive
- the quieter candidate establishes the audition reference
- the calibration manifest records model/evidence provenance
- `phase5_make_blind_bundle.py` creates only anonymous listener A/B assets plus the public response template
- the private key remains outside listener material
- `phase5_decode_blind_responses.py` correctly recovers the hidden guided candidate after responses are locked
- `phase5_mode1_calibration.py` refuses approval without an explicit policy

## Acceptance gates that must remain closed today

### Automatic Mode 1

`models/registry.json` must remain:

```json
"automaticMode1Approved": false
```

until a real listening/damage corpus satisfies an explicit acceptance policy.

### Mode 2 reconstruction

The production HTDemucs contract remains:

```json
"completeReconstruction": false
```

The normal desktop mastering path must not enable reconstruction.

### CUDA preference

CUDA is not a Phase 5 acceptance requirement. CPU remains the supported default/fallback execution path.

### Phase 6 instrument inference

Specific instrument identities such as 808, Rhodes, Wurlitzer, electric bass, synth bass, guitar variants, brass, saxophone, flute, individual drums, and similar labels belong to Phase 6 hierarchical instrument intelligence.

Phase 5 should not make exact instrument claims that exceed its separated-source evidence.

## Definition of Phase 5 accepted

Phase 5 is accepted when all of the following are true:

1. `windows-msvc` configures and builds locally.
2. Local C++ and Python Phase 5 tests pass.
3. The model registry validates.
4. The trusted HTDemucs artifact installs and verifies successfully.
5. `amt_worker` performs real four-stem inference.
6. Source-specific diagnostics reach the desktop when evidence supports them.
7. Stereo fallback remains usable when ML/model infrastructure is unavailable.
8. Compatible repeated separation requests use the cache safely.
9. Calibration candidate generation succeeds.
10. Attenuation-only blind audition generation succeeds.
11. Blind response decoding succeeds.
12. Policy analysis succeeds and cannot approve a model without passing evidence.
13. Automatic Mode 1 remains gated until a real policy corpus passes.
14. Mode 2 remains disabled in the desktop path.
15. No known source-level placeholders remain in the Phase 5 production path.

After these checks pass, Phase 6 may begin. Do not use Phase 6 work as a substitute for Phase 5 runtime acceptance.
