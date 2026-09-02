# Model licences and provenance

AudioMasteringTool does not embed model weights in the repository, portable
archive, or installer. The Windows application can download an approved artifact
on demand into the user's local application-data directory.

## HTDemucs ONNX FP16 weights

- Registry ID: `htdemucs-onnx-fp16weights`
- Source: `StemSplitio/htdemucs-onnx`, converted from Meta Demucs HTDemucs
- Pinned revision: `d54ed9eb60e258ea82131c6ee14578628816456a`
- Artifact: `htdemucs_fp16weights.onnx`
- SHA-256: `d05c269db7e4e50474ed9fa5759fad70b8063887c7158be0a7d8fc1adcfdb70a`
- Declared code licence: MIT
- Declared weights licence: MIT
- Distribution model: user-initiated, hash-verified download; not redistributed
  inside the AudioMasteringTool package

The machine-readable authority is `models/registry.json`, and the hard-coded
trusted download identity in `ModelArtifactInstaller.cpp` must match it exactly.
Automatic source-guided Mode 1 changes remain disabled until AudioMasteringTool's
own listening/damage policy passes; the upstream model benchmark is not a
substitute for product acceptance.

## Instrument classification

No production-approved 28-class instrument-classifier weights are configured.
The taxonomy and inference interfaces do not authorize exact instrument claims
without a separately reviewed model, corpus provenance, confidence calibration,
and acceptance evidence.

Before adding or changing any model, record code and weight licences separately,
commercial-use and redistribution decisions, source revision, artifact hash,
security review, resource requirements, evaluation corpus, and calibration
evidence in the registry and release record.
