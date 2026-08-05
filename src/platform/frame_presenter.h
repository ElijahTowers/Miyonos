#pragma once

#include <SDL.h>

#include <cstddef>
#include <string>

#include "platform/runtime_mode.h"

namespace miyonos {

#ifdef MIYONOS_ENABLE_SIMULATOR
class SimulatorShell;
#endif

// Every runtime draws the app into the same 640 x 480 software framebuffer.
// The physical build copies it directly to the double-buffered Linux
// framebuffer; desktop presents it through SDL and the simulator places it
// inside its handheld shell.
class FramePresenter {
 public:
  FramePresenter() = default;
  ~FramePresenter();

  FramePresenter(const FramePresenter&) = delete;
  FramePresenter& operator=(const FramePresenter&) = delete;

  bool initialize(SDL_Window* window, RuntimeMode mode, bool screen_only,
                  const std::string& scenario, bool live_sonos);
  SDL_Renderer* drawing_renderer() const { return drawing_renderer_; }
  Uint32 frame_format() const { return frame_format_; }
  bool present();
  void show_onion_home(uint32_t duration_ms);
#ifdef MIYONOS_ENABLE_SIMULATOR
  bool capture_bmp(const std::string& path) const;
  bool capture_presented_bmp(const std::string& path) const;
#endif
  const std::string& error() const { return error_; }

 private:
  void reset();
  bool fail(const std::string& operation);

  SDL_Renderer* presenter_ = nullptr;
  SDL_Surface* frame_surface_ = nullptr;
  SDL_Surface* presentation_surface_ = nullptr;
  SDL_Renderer* drawing_renderer_ = nullptr;
  SDL_Texture* frame_texture_ = nullptr;
  Uint32 frame_format_ = SDL_PIXELFORMAT_UNKNOWN;
#ifdef MIYONOS_ONIONOS
  int framebuffer_fd_ = -1;
  void* framebuffer_memory_ = nullptr;
  std::size_t framebuffer_memory_size_ = 0;
  int framebuffer_line_length_ = 0;
  int framebuffer_page_height_ = 0;
  int framebuffer_next_yoffset_ = 0;
#endif
  RuntimeMode mode_ = RuntimeMode::Desktop;
  bool screen_only_ = false;
#ifdef MIYONOS_ENABLE_SIMULATOR
  SimulatorShell* simulator_ = nullptr;
#endif
  std::string error_;
};

}  // namespace miyonos
