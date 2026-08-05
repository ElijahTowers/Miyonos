#pragma once

#include <SDL.h>

#include <string>

#include "platform/action.h"

namespace miyonos {

constexpr int kSimulatorWindowWidth = 1024;
constexpr int kSimulatorWindowHeight = 820;

Action simulator_hit_test(int x, int y);
void simulator_note_press(Action action, bool pressed);

class SimulatorShell {
 public:
  SimulatorShell(std::string scenario, bool live_sonos);

  void draw(SDL_Renderer* renderer, SDL_Texture* app_frame,
            bool onion_home = false) const;

 private:
  std::string scenario_;
  bool live_sonos_ = false;
};

}  // namespace miyonos
