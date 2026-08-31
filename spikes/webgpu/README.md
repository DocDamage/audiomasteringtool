# Native WebGPU inference evaluation

Phase 0 evaluates ONNX Runtime's **native WebGPU execution provider** as a future cross-vendor inference path.

## Decision

- Do not make native WebGPU a Windows 1.0 blocker.
- Keep model/session contracts provider-neutral so WebGPU can be introduced per model after benchmarking.
- CUDA remains the first high-performance desktop GPU path on supported NVIDIA hardware.
- CPU remains mandatory fallback.
- Browser work later uses compatible ONNX Runtime Web/WebGPU contracts or cloud fallback.

## Technical findings

Current ONNX Runtime exposes a native WebGPU EP built around Dawn and documents `onnxruntime_USE_WEBGPU=ON` for source builds. Provider maturity, operator coverage, memory behavior, and model-specific performance must be benchmarked before production selection.

A source-build spike is intentionally not included in the default Phase 0 CI because building Dawn/ORT from source is much heavier than validating the provider abstraction itself. The decision boundary is preserved by `IInferenceBackend`.
