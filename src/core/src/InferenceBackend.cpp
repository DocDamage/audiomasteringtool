#include "amt/core/InferenceBackend.h"

namespace amt::core {
namespace {
class CpuStubBackend final : public IInferenceBackend {
 public:
  InferenceDevice device() const noexcept override { return InferenceDevice::Cpu; }
  InferenceResult run(const InferenceRequest& request) override {
    if (request.model_id.empty()) {
      return {.ok = false, .output = {}, .error = "model_id is required"};
    }
    return {.ok = true, .output = request.input, .error = {}};
  }
};
}

std::unique_ptr<IInferenceBackend> make_cpu_inference_backend() {
  return std::make_unique<CpuStubBackend>();
}
}
