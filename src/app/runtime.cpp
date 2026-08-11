#include "app/runtime.h"

#include <SDL.h>

#include <algorithm>
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
#ifdef MIYONOS_ENABLE_SIMULATOR
  bool controls_shown = false;
  bool queue_opened = false;
  bool speakers_shown = false;
  bool playlist_started = false;
  bool playlist_tail_opened = false;
#endif
  int exit_status = 0;
#ifdef MIYONOS_ENABLE_SIMULATOR
  const uint32_t started_at = SDL_GetTicks();
#endif
  uint32_t next_frame = SDL_GetTicks();
  bool idle_frame_presented = false;
  auto process_event = [&](const SDL_Event& event) {
    if (event.type == SDL_QUIT) {
      running = false;
      window_closed = true;
      return;
    }
    // The first physical button after Battery Saver is a wake-only action.
    // It restores the static app frame without also pausing music, changing a
    // track, or opening a screen. Input remembers that one press until its
    // release, which also covers R2's delayed short-press behavior.
    bool physical_press = event.type == SDL_KEYDOWN ||
                          event.type == SDL_CONTROLLERBUTTONDOWN;
#ifdef MIYONOS_ENABLE_SIMULATOR
    physical_press = physical_press || event.type == SDL_MOUSEBUTTONDOWN;
#endif
    const bool wake_only =
        physical_press && controller.view().idle_battery_saver_active;
    if (physical_press) {
      controller.note_user_activity();
    }
    const Action action = input.translate(event);
    if (wake_only) input.suppress_current_press();
    if (controller.settings().diagnostics_mode &&
        (event.type == SDL_KEYDOWN ||
         event.type == SDL_CONTROLLERBUTTONDOWN)) {
      controller.record_input_code(input.last_keycode());
    }
    if (!wake_only && action != Action::None) controller.handle(action);
  };
  while (running && !controller.exit_requested()) {
    input.set_diagnostics(controller.settings().diagnostics_mode);
    input.set_button_mapping(controller.settings().button_mapping);
    if (controller.view().idle_battery_saver_active) {
      // Never make a button wait for the one-second black-frame cadence.
      // This keeps wake latency bounded while removing the active 30 FPS loop.
      SDL_Event idle_event;
      if (SDL_WaitEventTimeout(&idle_event, 100)) process_event(idle_event);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      process_event(event);
      if (!running) break;
    }
    if (!running) break;
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
    if (options_.show_queue_on_start && !queue_opened &&
        controller.view().screen == Screen::NowPlaying) {
      controller.handle(Action::Queue);
      queue_opened = true;
    }
    if (options_.show_speakers_on_start && !speakers_shown) {
      if (controller.view().screen == Screen::NowPlaying) {
        controller.handle(Action::Menu);
      } else if (controller.view().screen == Screen::Menu) {
        controller.handle(Action::Previous);
        controller.handle(Action::Down);
        controller.handle(Action::Confirm);
        speakers_shown = true;
      }
    }
    if (options_.show_playlist_on_start && !playlist_started) {
      if (controller.view().screen == Screen::NowPlaying) {
        controller.handle(Action::Queue);
      } else if (controller.view().screen == Screen::Queue) {
        controller.handle(Action::Context);
      } else if (controller.view().screen == Screen::Playlists &&
                 !controller.view().playlists.empty()) {
        controller.handle(Action::Confirm);
        playlist_started = true;
      }
    }
    if (options_.show_playlist_tail_on_start) {
      if (controller.view().screen == Screen::NowPlaying &&
          !playlist_tail_opened) {
        controller.handle(Action::Queue);
        playlist_tail_opened = true;
      } else if (controller.view().screen == Screen::Queue) {
        controller.handle(Action::Context);
      } else if (controller.view().screen == Screen::Playlists &&
                 !controller.view().busy) {
        controller.handle(Action::Next);
      }
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

    const bool idle_battery_saver =
        controller.view().idle_battery_saver_active;
    // Preserve one dimmed frame, then stop presenting while idle. OnionOS can
    // wake its LCD with the power key without generating an SDL input event;
    // retaining the frame makes the UI visible immediately after that wake.
    if (!idle_battery_saver || !idle_frame_presented) {
      renderer.draw(controller.view(), controller.settings());
      if (!frames.present()) {
        std::cerr << "Miyonos could not present its display: "
                  << frames.error() << '\n';
        exit_status = 1;
        break;
      }
      idle_frame_presented = idle_battery_saver;
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
    if (idle_battery_saver) {
      next_frame = SDL_GetTicks();
      continue;
    }
    idle_frame_presented = false;
    next_frame += 33;
    const uint32_t frame_now = SDL_GetTicks();
    if (next_frame > frame_now) {
      SDL_Delay(next_frame - frame_now);
    } else {
      next_frame = frame_now;
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
