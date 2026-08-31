#include <cassert>
#include <string>
#include "amt/core/AnalysisTypes.h"
#include "amt/core/InferenceBackend.h"
#include "amt/core/Version.h"

int main() {
  assert(!amt::core::version().empty());
  assert(amt::core::is_valid_confidence(0.0F));
  assert(amt::core::is_valid_confidence(1.0F));
  assert(!amt::core::is_valid_confidence(-0.1F));
  assert(!amt::core::is_valid_confidence(1.1F));

  auto backend = amt::core::make_cpu_inference_backend();
  auto ok = backend->run({.model_id = "smoke", .input = {0.5F}});
  assert(ok.ok);
  assert(ok.output.size() == 1);

  auto bad = backend->run({.model_id = "", .input = {}});
  assert(!bad.ok);
  return 0;
}
