#include <SDL.h>

#include <iostream>
#include <string>

#include "app/controller.h"
#include "platform/clock.h"
#include "ui/renderer.h"

using namespace miyonos;

int main(int argc, char** argv) {
  const std::string output = argc > 1 ? argv[1] : "miyonos-preview.bmp";
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << SDL_GetError() << '\n';
    return 1;
  }
  SDL_Surface* surface =
      SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 24, SDL_PIXELFORMAT_RGB24);
  SDL_Renderer* software = SDL_CreateSoftwareRenderer(surface);
  if (!surface || !software) {
    std::cerr << SDL_GetError() << '\n';
    return 1;
  }

  ViewState view;
  view.screen = Screen::NowPlaying;
  view.connected = true;
  view.active_group_id = "preview-group";
  view.topology.groups.push_back(
      {"preview-group", "preview-player",
       {"preview-player", "preview-kitchen"}, "Living Room + Kitchen"});
  Player player;
  player.uuid = "preview-player";
  player.room_name = "Living Room";
  view.topology.players.push_back(player);
  view.active_room_uuid = player.uuid;
  view.playback.state = TransportState::Playing;
  view.playback.track.title = "Handheld Sessions";
  view.playback.track.artist = "Miyonos Ensemble";
  view.playback.track.album = "Local Sessions";
  view.playback.track.elapsed_seconds = 77;
  view.playback.track.duration_seconds = 222;
  view.playback.track.seekable = true;
  view.playback.volume = 28;
  view.speaker_volume = 28;
  view.last_input_ms = monotonic_ms();

  Settings settings;
  settings.button_hints = ButtonHints::Always;
  Renderer renderer(software);
  renderer.draw(view, settings);
  const int result = SDL_SaveBMP(surface, output.c_str());
  SDL_DestroyRenderer(software);
  SDL_FreeSurface(surface);
  SDL_Quit();
  return result == 0 ? 0 : 1;
}
