#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: amt_onnx_spike <model.onnx> [cpu|cuda]\n";
    return 2;
  }

  const std::string provider = argc == 3 ? argv[2] : "cpu";
  if (provider != "cpu" && provider != "cuda") {
    std::cerr << "provider must be cpu or cuda\n";
    return 2;
  }

  Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "amt-phase0-spike");
  Ort::SessionOptions options;
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

  if (provider == "cuda") {
#ifdef AMT_ORT_ENABLE_CUDA
    const auto& api = Ort::GetApi();
    OrtCUDAProviderOptionsV2* cuda_options = nullptr;
    Ort::ThrowOnError(api.CreateCUDAProviderOptions(&cuda_options));
    Ort::ThrowOnError(api.SessionOptionsAppendExecutionProvider_CUDA_V2(
        static_cast<OrtSessionOptions*>(options), cuda_options));
    api.ReleaseCUDAProviderOptions(cuda_options);
#else
    std::cerr << "CUDA provider not compiled into this spike\n";
    return 3;
#endif
  }

  const std::filesystem::path model_path(argv[1]);
  Ort::Session session(env, model_path.c_str(), options);

  std::array<float, 4> input{-1.0F, 2.0F, -3.0F, 4.0F};
  const std::array<std::int64_t, 2> shape{1, 4};
  auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  auto input_tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());

  const char* input_names[] = {"X"};
  const char* output_names[] = {"Y"};
  auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
  if (outputs.size() != 1 || !outputs[0].IsTensor()) {
    std::cerr << "unexpected inference output\n";
    return 4;
  }

  const float* values = outputs[0].GetTensorData<float>();
  const std::array<float, 4> expected{1.0F, 2.0F, 3.0F, 4.0F};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (std::fabs(values[i] - expected[i]) > 1.0e-6F) {
      std::cerr << "inference mismatch at " << i << '\n';
      return 5;
    }
  }

  std::cout << "ONNX Runtime " << provider << " inference: ok\n";
  return 0;
}
