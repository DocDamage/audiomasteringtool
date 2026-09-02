#include <cassert>
#include <string>

#include "amt/codec/AudioIO.h"
#include "amt/codec/SndFileCodec.h"
#include "amt/codec/SndFileDynamic.h"

#include "amt/core/AnalysisTypes.h"
#include "amt/core/InferenceBackend.h"
#include "amt/core/Version.h"

int main() {
  assert(amt::core::version() == "1.0.0");
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

  amt::codec::SndFileCodecService service;
  auto caps = service.capabilities();
  assert(caps.size() >= 7); // WAV, AIFF, FLAC, MP3, AAC/M4A, OGG, Opus

  bool has_mp3 = false;
  bool has_aac = false;
  bool has_ogg = false;
  bool has_opus = false;
  for (const auto& cap : caps) {
    if (cap.container == amt::codec::AudioContainer::mp3) has_mp3 = true;
    if (cap.container == amt::codec::AudioContainer::aac_m4a) has_aac = true;
    if (cap.container == amt::codec::AudioContainer::ogg) has_ogg = true;
    if (cap.container == amt::codec::AudioContainer::opus) has_opus = true;
  }
  assert(has_mp3 && has_aac && has_ogg && has_opus);

  return 0;
}
