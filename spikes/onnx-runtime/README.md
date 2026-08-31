# ONNX Runtime C++ spike

Purpose: prove that the production C++ process can execute the same trivial ONNX graph on CPU and on CUDA without leaking provider APIs into `amt_core`.

Pinned evaluation runtime: **ONNX Runtime 1.29.0**.

The test model computes `abs([-1, 2, -3, 4])` and verifies `[1, 2, 3, 4]`.

## CPU

1. Extract the official `onnxruntime-win-x64-1.29.0.zip` package.
2. Generate `phase0_abs.onnx` with `generate_model.py` (requires the Python `onnx` package only for the spike).
3. Configure this directory with `-DONNXRUNTIME_ROOT=<extracted package>`.
4. Build and run `amt_onnx_spike phase0_abs.onnx cpu`.

## CUDA

Use the official GPU package matching the installed CUDA major version and configure with `-DAMT_ORT_ENABLE_CUDA=ON`. Run `amt_onnx_spike phase0_abs.onnx cuda` on an NVIDIA-equipped machine.

CUDA execution is intentionally a worker/offline capability. The future VST real-time thread must never depend on GPU inference succeeding.

The GitHub workflow contains a standard CPU job and an opt-in self-hosted NVIDIA job because GitHub-hosted Windows runners do not provide the required NVIDIA GPU.
