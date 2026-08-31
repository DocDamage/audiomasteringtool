#include <cassert>
#include <string>

#include "amt/codec/SndFileDynamic.h"
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

  assert(amt::codec::is_phase0_baseline_format(0x010000 | 0x0002));  // WAV PCM16
  assert(amt::codec::is_phase0_baseline_format(0x170000 | 0x0003));  // FLAC PCM24
  assert(!amt::codec::is_phase0_baseline_format(0x010000 | 0x0006)); // WAV float
  return 0;
}
