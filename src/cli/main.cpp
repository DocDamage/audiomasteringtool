#include <filesystem>
#include <iostream>
#include <string>

#include "amt/codec/SndFileDynamic.h"
#include "amt/core/Version.h"

namespace {
void usage() {
  std::cout << "AudioMasteringTool Phase 0 CLI\n"
            << "  amt_cli --version\n"
            << "  amt_cli codec-status\n"
            << "  amt_cli probe <input.wav|input.flac>\n"
            << "  amt_cli rerender <input> <output>\n"
            << "  amt_cli verify <first> <second>\n";
}
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }

  const std::string command = argv[1];
  if (command == "--version") {
    std::cout << amt::core::version() << '\n';
    return 0;
  }

  amt::codec::SndFileRuntime runtime;
  if (command == "codec-status") {
    if (!runtime.available()) {
      std::cerr << "unavailable: " << runtime.load_error() << '\n';
      return 1;
    }
    std::cout << "libsndfile: available\n";
    return 0;
  }

  if (!runtime.available()) {
    std::cerr << "codec error: " << runtime.load_error() << '\n';
    return 1;
  }

  std::string error;
  if (command == "probe" && argc == 3) {
    const auto info = runtime.probe(argv[2], error);
    if (!info) {
      std::cerr << "probe failed: " << error << '\n';
      return 1;
    }
    std::cout << "container=" << info->container << " subtype=" << info->subtype
              << " rate=" << info->sample_rate << " channels=" << info->channels
              << " frames=" << info->frames << '\n';
    return 0;
  }

  if (command == "rerender" && argc == 4) {
    if (!runtime.lossless_rerender(argv[2], argv[3], error)) {
      std::cerr << "rerender failed: " << error << '\n';
      return 1;
    }
    std::cout << "rerender: ok\n";
    return 0;
  }

  if (command == "verify" && argc == 4) {
    if (!runtime.verify_pcm_equal(argv[2], argv[3], error)) {
      std::cerr << "verify failed: " << error << '\n';
      return 1;
    }
    std::cout << "verify: decoded PCM is lossless\n";
    return 0;
  }

  usage();
  return 2;
}
