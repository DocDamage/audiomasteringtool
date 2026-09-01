#include "amt/app/DesktopApplication.h"

#include <iostream>

namespace amt::app {

DesktopApplication::DesktopApplication()
    : session_(std::make_unique<MasteringSessionController>()) {}

int DesktopApplication::run(int argc, char* argv[]) {
  if (argc > 1) {
    const std::filesystem::path input_path(argv[1]);
    std::string error;
    if (!session_->open_source(input_path, error)) {
      std::cerr << "Failed to open input audio: " << error << std::endl;
      return 1;
    }
    if (!session_->run_mastering(error)) {
      std::cerr << "Mastering failed: " << error << std::endl;
      return 1;
    }
    std::cout << "Mastering completed successfully for: " << input_path << std::endl;
  }
  return 0;
}

}  // namespace amt::app
