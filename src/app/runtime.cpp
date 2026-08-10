#include "app/runtime.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>

#include "app/controller.h"
#include "platform/frame_presenter.h"
#include "platform/input.h"
#include "ui/renderer.h"

#ifdef MIYONOS_ENABLE_SIMULATOR
#include "simulator/mock_sonos.h"
#endif

namespace miyonos {

int AppRuntime::run(FramePresenter& frames) {
#ifdef MIYONOS_ENABLE_SIMULATOR
  std::unique_ptr<SimulatorSonosFixture> fixture;
  const bool use_embedded_fixture =
      options_.mode == RuntimeMode::Simulator && !options_.live_sonos &&
      options_.scenario != "offline" &&
      std::getenv("MIYONOS_EXTERNAL_FIXTURE") == nullptr;
  if (use_embedded_fixture) {
    fixture = std::make_unique<SimulatorSonosFixture>(options_.scenario);
    if (!fixture->start()) {
      std::cerr << "Miyonos simulator fixture failed: " << fixture->error()
                << '\n';
      return 1;
    }
  }
#endif
  Controller controller(options_.data_directory);
  Input input(options_.mode);
  Renderer renderer(frames.drawing_renderer());
  controller.start();

  bool running = true;
  bool window_closed = false;
  bool stay_awake_file = false;
  bool captured = false;
  bool controls_shown = false;
  int exit_status = 0;
#ifdef MIYONOS_ENABLE_SIMULATOR
  const uint32_t started_at = SDL_GetTicks();
#endif
  uint32_t next_frame = SDL_GetTicks();
  while (running && !controller.exit_requested()) {
    input.set_diagnostics(controller.settings().diagnostics_mode);
    input.set_button_mapping(controller.settings().button_mapping);
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
        window_closed = true;
        break;
      }
      const Action action = input.translate(event);
      if (controller.settings().diagnostics_mode &&
          (event.type == SDL_KEYDOWN ||
           event.type == SDL_CONTROLLERBUTTONDOWN)) {
        controller.record_input_code(input.last_keycode());
      }
      if (action != Action::None) controller.handle(action);
    }
    const Action repeated = input.repeat_action(SDL_GetTicks());
    if (repeated != Action::None) controller.handle(repeated);

    controller.update();
#ifdef MIYONOS_ENABLE_SIMULATOR
    if (options_.show_controls_on_start && !controls_shown &&
        controller.view().screen != Screen::Splash &&
        controller.view().screen != Screen::Discovery) {
      controller.handle(Action::Controls);
      controls_shown = true;
    }
#endif
    if (controller.settings().prevent_sleep) {
      SDL_DisableScreenSaver();
      if (options_.mode == RuntimeMode::OnionOS && !stay_awake_file) {
        std::ofstream("/tmp/stay_awake", std::ios::app);
        stay_awake_file = true;
      }
    } else {
      SDL_EnableScreenSaver();
      if (options_.mode == RuntimeMode::OnionOS && stay_awake_file) {
        std::remove("/tmp/stay_awake");
        stay_awake_file = false;
      }
    }

    renderer.draw(controller.view(), controller.settings());
    if (!frames.present()) {
      std::cerr << "Miyonos could not present its display: "
                << frames.error() << '\n';
      exit_status = 1;
      break;
    }
#ifdef MIYONOS_ENABLE_SIMULATOR
    if ((!options_.capture_path.empty() ||
         !options_.capture_shell_path.empty()) && !captured &&
        SDL_GetTicks() - started_at >= options_.capture_after_ms) {
      captured = true;
      if (!options_.capture_path.empty() &&
          !frames.capture_bmp(options_.capture_path)) {
        std::cerr << "Miyonos could not save its test frame: "
                  << SDL_GetError() << '\n';
        exit_status = 1;
      }
      if (!options_.capture_shell_path.empty() &&
          !frames.capture_presented_bmp(options_.capture_shell_path)) {
        std::cerr << "Miyonos could not save its simulator shell: "
                  << SDL_GetError() << '\n';
        exit_status = 1;
      }
      running = false;
    }
#endif
    next_frame += 33;
    const uint32_t now = SDL_GetTicks();
    if (next_frame > now) {
      SDL_Delay(next_frame - now);
    } else {
      next_frame = now;
    }
  }

  controller.stop();
#ifdef MIYONOS_ENABLE_SIMULATOR
  fixture.reset();
#endif
  if (stay_awake_file) std::remove("/tmp/stay_awake");
  if (options_.mode == RuntimeMode::Simulator && !window_closed && !captured &&
      exit_status == 0) {
    frames.show_onion_home(2200);
  }
  return exit_status;
}

}  // namespace miyonos
