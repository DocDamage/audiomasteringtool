# ADR 0003: Hardware-abstracted inference

Status: Accepted

## Decision

Domain code addresses stable model IDs through `IInferenceBackend`; execution providers are selected externally.

Initial targets are CPU and NVIDIA CUDA on desktop, with cross-vendor GPU/cloud and browser WebGPU possible later. No mastering policy may depend on a provider-specific API.
