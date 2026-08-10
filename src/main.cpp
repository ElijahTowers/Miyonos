#include <SDL.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef MIYONOS_ENABLE_SIMULATOR
#include <set>
#endif

#include "app/runtime.h"
#include "platform/frame_presenter.h"
#include "platform/runtime_mode.h"

#ifdef MIYONOS_ENABLE_SIMULATOR
#include "simulator/simulator_shell.h"
#endif

namespace fs = std::filesystem;

namespace {

std::string default_data_directory(const char* argv0) {
  if (const char* configured = std::getenv("MIYONOS_DATA_DIR")) {
    return configured;
  }
  std::error_code ec;
  fs::path executable = fs::absolute(argv0 ? argv0 : "miyonos", ec);
  if (!ec) return (executable.parent_path() / "data").string();
  return "data";
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  int scale = 1;
  bool frame_presenter_smoke = false;
  bool device_display_probe = false;
#ifdef MIYONOS_ENABLE_SIMULATOR
  bool simulator = false;
  bool screen_only = false;
  bool live_sonos = false;
  bool show_controls = false;
  bool show_queue = false;
  bool show_playlist = false;
  bool show_playlist_tail = false;
  bool data_directory_explicit = std::getenv("MIYONOS_DATA_DIR") != nullptr;
  std::string scenario = "grouped";
  std::string capture_path;
  std::string capture_shell_path;
  uint32_t capture_after_ms = 0;
#else
  constexpr bool screen_only = false;
  const std::string scenario;
  const std::string capture_path;
  const std::string capture_shell_path;
  constexpr bool live_sonos = false;
  constexpr bool show_controls = false;
  constexpr bool show_queue = false;
  constexpr bool show_playlist = false;
  constexpr bool show_playlist_tail = false;
  constexpr uint32_t capture_after_ms = 0;
#endif
  std::string data_directory = default_data_directory(argc > 0 ? argv[0] : nullptr);
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--scale" && i + 1 < argc) {
      scale = std::max(1, std::min(4, std::atoi(argv[++i])));
    } else if (argument == "--data-dir" && i + 1 < argc) {
      data_directory = argv[++i];
#ifdef MIYONOS_ENABLE_SIMULATOR
      data_directory_explicit = true;
#endif
    } else if (argument == "--player-ip" && i + 1 < argc) {
#ifdef _WIN32
      _putenv_s("MIYONOS_PLAYER_IP", argv[++i]);
#else
      setenv("MIYONOS_PLAYER_IP", argv[++i], 1);
#endif
    } else if (argument == "--version") {
      std::cout << "Miyonos " << MIYONOS_VERSION << '\n';
      return 0;
    } else if (argument == "--frame-presenter-smoke") {
      frame_presenter_smoke = true;
    } else if (argument == "--device-display-probe") {
      device_display_probe = true;
#ifdef MIYONOS_ENABLE_SIMULATOR
    } else if (argument == "--simulator") {
      simulator = true;
    } else if (argument == "--screen-only") {
      simulator = true;
      screen_only = true;
    } else if (argument == "--show-controls") {
      simulator = true;
      show_controls = true;
    } else if (argument == "--show-queue") {
      simulator = true;
      show_queue = true;
    } else if (argument == "--show-playlist") {
      simulator = true;
      show_playlist = true;
    } else if (argument == "--show-playlist-tail") {
      simulator = true;
      show_playlist_tail = true;
    } else if (argument == "--live-sonos") {
      simulator = true;
      live_sonos = true;
    } else if (argument == "--scenario" && i + 1 < argc) {
      simulator = true;
      scenario = argv[++i];
    } else if (argument == "--capture-frame" && i + 1 < argc) {
      capture_path = argv[++i];
    } else if (argument == "--capture-shell" && i + 1 < argc) {
      simulator = true;
      capture_shell_path = argv[++i];
    } else if (argument == "--capture-after-ms" && i + 1 < argc) {
      capture_after_ms = static_cast<uint32_t>(
          std::max(0, std::min(30000, std::atoi(argv[++i]))));
#endif
    } else if (argument == "--help") {
      std::cout
          << "Usage: miyonos [--scale 1-4] [--data-dir PATH] "
             "[--player-ip IP]\n";
      std::cout
          << "Device diagnostics: [--frame-presenter-smoke] "
             "[--device-display-probe]\n";
#ifdef MIYONOS_ENABLE_SIMULATOR
      std::cout
          << "       miyonos --simulator [--scenario NAME] "
             "[--live-sonos] [--screen-only] [--show-controls] [--show-queue] "
             "[--show-playlist] [--show-playlist-tail]\n"
          << "Test capture: [--capture-frame PATH] [--capture-shell PATH] "
             "[--capture-after-ms N]\n";
#endif
      return 0;
    }
  }

#ifdef MIYONOS_ENABLE_SIMULATOR
  const std::set<std::string> scenarios{
      "normal",       "multi-room", "grouped", "long-queue",
      "mixed-favorites", "no-artwork", "slow", "offline",
      "coordinator-change"};
  if (simulator && !live_sonos && scenarios.count(scenario) == 0) {
    std::cerr << "Unknown simulator scenario: " << scenario << '\n';
    return 2;
  }
#endif

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER |
               SDL_INIT_GAMECONTROLLER) != 0) {
    std::cerr << "Miyonos could not initialize SDL: " << SDL_GetError() << '\n';
    return 1;
  }

#ifdef MIYONOS_ENABLE_SIMULATOR
  const bool handheld = !simulator &&
      (std::getenv("MIYONOS_DEVICE") != nullptr ||
      (SDL_GetCurrentVideoDriver() &&
       std::string(SDL_GetCurrentVideoDriver()) == "Mini"));
  const miyonos::RuntimeMode mode =
      handheld ? miyonos::RuntimeMode::OnionOS
               : simulator ? miyonos::RuntimeMode::Simulator
                           : miyonos::RuntimeMode::Desktop;

  if (mode == miyonos::RuntimeMode::Simulator && !live_sonos) {
#ifdef _WIN32
    _putenv_s("MIYONOS_DISABLE_SSDP", "1");
    _putenv_s("MIYONOS_PLAYER_IP", "127.0.0.1");
#else
    setenv("MIYONOS_DISABLE_SSDP", "1", 1);
    setenv("MIYONOS_PLAYER_IP", "127.0.0.1", 0);
#endif
  }

  if (mode == miyonos::RuntimeMode::Simulator && !data_directory_explicit) {
    char* preference_path = SDL_GetPrefPath("Miyonos", "Simulator");
    if (preference_path) {
      data_directory =
          (fs::path(preference_path) / "SDCARD/App/Miyonos/data").string();
      SDL_free(preference_path);
    }
  }
#else
  const bool handheld =
      std::getenv("MIYONOS_DEVICE") != nullptr ||
      (SDL_GetCurrentVideoDriver() &&
       std::string(SDL_GetCurrentVideoDriver()) == "Mini");
  const miyonos::RuntimeMode mode =
      handheld ? miyonos::RuntimeMode::OnionOS
               : miyonos::RuntimeMode::Desktop;
#endif

  const Uint32 window_flags = SDL_WINDOW_SHOWN;
#ifdef MIYONOS_ENABLE_SIMULATOR
  const int base_width =
      handheld ? 320
      : mode == miyonos::RuntimeMode::Simulator && !screen_only
          ? miyonos::kSimulatorWindowWidth
          : 640;
  const int base_height =
      handheld ? 240
      : mode == miyonos::RuntimeMode::Simulator && !screen_only
          ? miyonos::kSimulatorWindowHeight
          : 480;
#else
  const int base_width = handheld ? 320 : 640;
  const int base_height = handheld ? 240 : 480;
#endif
  const int window_x =
      handheld ? SDL_WINDOWPOS_UNDEFINED : SDL_WINDOWPOS_CENTERED;
  const int window_y =
      handheld ? SDL_WINDOWPOS_UNDEFINED : SDL_WINDOWPOS_CENTERED;
  SDL_Window* window =
      SDL_CreateWindow("Miyonos", window_x, window_y, base_width * scale,
                       base_height * scale, window_flags);
  if (!window) {
    std::cerr << "Miyonos could not create its window: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }
  int exit_status = 0;
  {
    miyonos::FramePresenter frames;
    if (!frames.initialize(window, mode, screen_only, scenario, live_sonos)) {
      std::cerr << "Miyonos could not initialize its display: "
                << frames.error() << '\n';
      exit_status = 1;
    } else if (frame_presenter_smoke || device_display_probe) {
      SDL_Renderer* renderer = frames.drawing_renderer();
      SDL_SetRenderDrawColor(renderer, 7, 20, 43, 255);
      SDL_RenderClear(renderer);
      SDL_SetRenderDrawColor(renderer, 113, 214, 177, 255);
      SDL_Rect mint{32, 32, 576, 104};
      SDL_RenderFillRect(renderer, &mint);
      SDL_SetRenderDrawColor(renderer, 255, 115, 84, 255);
      SDL_Rect coral{32, 188, 576, 104};
      SDL_RenderFillRect(renderer, &coral);
      SDL_SetRenderDrawColor(renderer, 255, 241, 207, 255);
      SDL_Rect cream{32, 344, 576, 104};
      SDL_RenderFillRect(renderer, &cream);
      SDL_RenderPresent(renderer);
      if (!frames.present()) {
        std::cerr << "Miyonos frame-presenter smoke check failed: "
                  << frames.error() << '\n';
        exit_status = 1;
      } else if (device_display_probe) {
        std::cerr << "Miyonos display probe: "
                  << SDL_GetPixelFormatName(frames.frame_format()) << '\n';
        const uint32_t until = SDL_GetTicks() + 8000;
        while (SDL_GetTicks() < until) {
          SDL_Event event;
          bool stop = false;
          while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN) {
              stop = true;
            }
          }
          if (stop) break;
          SDL_Delay(20);
        }
      }
    } else {
      if (mode == miyonos::RuntimeMode::OnionOS) {
        std::cerr << "Miyonos display: direct 640x480 double-buffered "
                     "framebuffer presentation\n";
      }
      miyonos::AppRuntime runtime(
          {mode, data_directory, capture_path, capture_shell_path,
           scenario, live_sonos, show_controls, show_queue, show_playlist,
           show_playlist_tail, capture_after_ms});
      exit_status = runtime.run(frames);
    }
  }
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exit_status;
}
