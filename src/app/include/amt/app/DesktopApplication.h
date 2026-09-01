#pragma once

#include <memory>
#include <string>

#include "amt/app/MasteringSessionController.h"

namespace amt::app {

class DesktopApplication {
 public:
  DesktopApplication();
  ~DesktopApplication() = default;

  [[nodiscard]] MasteringSessionController& session() noexcept { return *session_; }
  [[nodiscard]] const MasteringSessionController& session() const noexcept { return *session_; }

  [[nodiscard]] int run(int argc, char* argv[]);

 private:
  std::unique_ptr<MasteringSessionController> session_;
};

}  // namespace amt::app
