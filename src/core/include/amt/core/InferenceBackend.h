#pragma once
#include <memory>
#include <string>
#include <vector>

namespace amt::core {

enum class InferenceDevice { Cpu, NvidiaCuda, WebGpu, Cloud };

struct InferenceRequest {
  std::string model_id;
  std::vector<float> input;
};

struct InferenceResult {
  bool ok{false};
  std::vector<float> output;
  std::string error;
};

class IInferenceBackend {
 public:
  virtual ~IInferenceBackend() = default;
  virtual InferenceDevice device() const noexcept = 0;
  virtual InferenceResult run(const InferenceRequest& request) = 0;
};

std::unique_ptr<IInferenceBackend> make_cpu_inference_backend();

}
