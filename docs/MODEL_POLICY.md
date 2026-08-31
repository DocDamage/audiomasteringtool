# Model and Dependency Policy

No model weight or dependency enters a release merely because its source code is open.

## Required metadata for each model

- stable model ID and semantic version
- source repository / publication
- exact weight artifact and checksum
- architecture/runtime format
- license for code
- license for weights
- commercial-use status
- attribution requirements
- training-data disclosures when known
- platform/provider compatibility
- expected RAM/VRAM footprint
- quality benchmark reference
- security review status

Unknown or ambiguous weight licenses are release blockers.

## Model manifest

All approved models must be declared in `models/registry.json`. Phase 0 contains no production model weights.
