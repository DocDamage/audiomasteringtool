#include <iostream>
#include <string>
#include "amt/core/InferenceBackend.h"
#include "amt/core/Version.h"

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--health") {
    std::cout << "{\"status\":\"ok\",\"version\":\"" << amt::core::version() << "\"}\n";
    return 0;
  }

  auto backend = amt::core::make_cpu_inference_backend();
  const auto result = backend->run({.model_id = "phase0-smoke", .input = {1.0F, 2.0F, 3.0F}});
  if (!result.ok) {
    std::cerr << result.error << '\n';
    return 1;
  }

  std::cout << "AudioMasteringTool worker ready (" << amt::core::version() << ")\n";
  return 0;
}
