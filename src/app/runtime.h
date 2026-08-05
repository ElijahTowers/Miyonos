#pragma once

#include <string>
#include <utility>

#include "platform/runtime_mode.h"

namespace miyonos {

class FramePresenter;

struct AppRuntimeOptions {
  RuntimeMode mode = RuntimeMode::Desktop;
  std::string data_directory;
  std::string capture_path;
  std::string capture_shell_path;
  std::string scenario = "grouped";
  bool live_sonos = false;
  uint32_t capture_after_ms = 0;
};

class AppRuntime {
 public:
  explicit AppRuntime(AppRuntimeOptions options)
      : options_(std::move(options)) {}

  int run(FramePresenter& frames);

 private:
  AppRuntimeOptions options_;
};

}  // namespace miyonos
