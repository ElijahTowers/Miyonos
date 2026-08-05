#include "platform/frame_presenter.h"

#ifdef MIYONOS_ONIONOS
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

#ifdef MIYONOS_ENABLE_SIMULATOR
#include "simulator/simulator_shell.h"
#endif

namespace miyonos {

namespace {

constexpr int kScreenWidth = 640;
constexpr int kScreenHeight = 480;
}  // namespace

FramePresenter::~FramePresenter() { reset(); }

void FramePresenter::reset() {
#ifdef MIYONOS_ENABLE_SIMULATOR
  delete simulator_;
  simulator_ = nullptr;
#endif
  if (frame_texture_) SDL_DestroyTexture(frame_texture_);
  if (presentation_surface_) SDL_FreeSurface(presentation_surface_);
  if (drawing_renderer_) SDL_DestroyRenderer(drawing_renderer_);
  if (frame_surface_) SDL_FreeSurface(frame_surface_);
  if (presenter_) SDL_DestroyRenderer(presenter_);
#ifdef MIYONOS_ONIONOS
  if (framebuffer_memory_) {
    munmap(framebuffer_memory_, framebuffer_memory_size_);
  }
  if (framebuffer_fd_ >= 0) close(framebuffer_fd_);
  framebuffer_fd_ = -1;
  framebuffer_memory_ = nullptr;
  framebuffer_memory_size_ = 0;
  framebuffer_line_length_ = 0;
  framebuffer_page_height_ = 0;
  framebuffer_next_yoffset_ = 0;
#endif
  frame_texture_ = nullptr;
  presentation_surface_ = nullptr;
  drawing_renderer_ = nullptr;
  frame_surface_ = nullptr;
  presenter_ = nullptr;
  frame_format_ = SDL_PIXELFORMAT_UNKNOWN;
  mode_ = RuntimeMode::Desktop;
  screen_only_ = false;
}

bool FramePresenter::fail(const std::string& operation) {
  error_ = operation + ": " + SDL_GetError();
  reset();
  return false;
}

bool FramePresenter::initialize(SDL_Window* window, RuntimeMode mode,
                                bool screen_only,
                                const std::string& scenario,
                                bool live_sonos) {
#ifndef MIYONOS_ENABLE_SIMULATOR
  (void)scenario;
  (void)live_sonos;
#endif
  reset();
  error_.clear();
  mode_ = mode;
  screen_only_ = screen_only;
#ifdef MIYONOS_ONIONOS
  if (mode_ == RuntimeMode::OnionOS) {
    framebuffer_fd_ = open("/dev/fb0", O_RDWR);
    if (framebuffer_fd_ < 0) {
      SDL_SetError("Could not open /dev/fb0: %s", std::strerror(errno));
      return fail("Could not initialize the direct device framebuffer");
    }

    fb_fix_screeninfo fixed{};
    fb_var_screeninfo variable{};
    if (ioctl(framebuffer_fd_, FBIOGET_FSCREENINFO, &fixed) != 0 ||
        ioctl(framebuffer_fd_, FBIOGET_VSCREENINFO, &variable) != 0) {
      SDL_SetError("Could not inspect /dev/fb0: %s", std::strerror(errno));
      return fail("Could not initialize the direct device framebuffer");
    }
    if (variable.xres < kScreenWidth || variable.yres < kScreenHeight ||
        variable.yres_virtual < variable.yres * 2 ||
        variable.bits_per_pixel != 32 ||
        fixed.line_length < kScreenWidth * 4 ||
        fixed.smem_len < fixed.line_length * variable.yres * 2) {
      SDL_SetError("Unsupported framebuffer geometry %ux%u, virtual %ux%u, "
                   "%u bpp, line %u",
                   variable.xres, variable.yres, variable.xres_virtual,
                   variable.yres_virtual, variable.bits_per_pixel,
                   fixed.line_length);
      return fail("Could not initialize the direct device framebuffer");
    }

    framebuffer_memory_size_ = fixed.smem_len;
    framebuffer_memory_ = mmap(nullptr, framebuffer_memory_size_,
                               PROT_READ | PROT_WRITE, MAP_SHARED,
                               framebuffer_fd_, 0);
    if (framebuffer_memory_ == MAP_FAILED) {
      framebuffer_memory_ = nullptr;
      SDL_SetError("Could not map /dev/fb0: %s", std::strerror(errno));
      return fail("Could not initialize the direct device framebuffer");
    }
    framebuffer_line_length_ = static_cast<int>(fixed.line_length);
    framebuffer_page_height_ = static_cast<int>(variable.yres);
    framebuffer_next_yoffset_ =
        variable.yoffset == 0 ? framebuffer_page_height_ : 0;
  } else {
#endif
  presenter_ = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!presenter_) {
    presenter_ = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!presenter_) return fail("Could not create the display renderer");
#ifdef MIYONOS_ONIONOS
  }
#endif

  if (presenter_) {
#ifdef MIYONOS_ENABLE_SIMULATOR
    if (mode_ == RuntimeMode::Simulator && !screen_only_) {
      SDL_RenderSetLogicalSize(presenter_, kSimulatorWindowWidth,
                               kSimulatorWindowHeight);
      simulator_ = new SimulatorShell(scenario, live_sonos);
    } else {
#endif
      SDL_RenderSetLogicalSize(presenter_, kScreenWidth, kScreenHeight);
      SDL_RenderSetIntegerScale(presenter_, SDL_TRUE);
#ifdef MIYONOS_ENABLE_SIMULATOR
    }
#endif
  }

  frame_format_ = SDL_PIXELFORMAT_ARGB8888;
  frame_surface_ = SDL_CreateRGBSurfaceWithFormat(
      0, kScreenWidth, kScreenHeight, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!frame_surface_) return fail("Could not allocate the device frame");

  drawing_renderer_ = SDL_CreateSoftwareRenderer(frame_surface_);
  if (!drawing_renderer_) {
    return fail("Could not create the device software renderer");
  }
  SDL_RenderSetLogicalSize(drawing_renderer_, kScreenWidth, kScreenHeight);
  SDL_RenderSetIntegerScale(drawing_renderer_, SDL_TRUE);

  if (presenter_) {
    frame_texture_ = SDL_CreateTexture(
        presenter_, frame_format_, SDL_TEXTUREACCESS_STREAMING, kScreenWidth,
        kScreenHeight);
    if (!frame_texture_) {
      return fail("Could not create the device frame texture");
    }
    SDL_SetTextureBlendMode(frame_texture_, SDL_BLENDMODE_NONE);
  }
  return true;
}

bool FramePresenter::present() {
#ifdef MIYONOS_ONIONOS
  if (framebuffer_memory_) {
    auto* page = static_cast<Uint8*>(framebuffer_memory_) +
                 static_cast<std::size_t>(framebuffer_next_yoffset_) *
                     framebuffer_line_length_;
    for (int y = 0; y < kScreenHeight; ++y) {
      const auto* source = reinterpret_cast<const Uint32*>(
          static_cast<const Uint8*>(frame_surface_->pixels) +
          static_cast<std::size_t>(kScreenHeight - 1 - y) *
              frame_surface_->pitch);
      auto* destination = reinterpret_cast<Uint32*>(
          page + static_cast<std::size_t>(y) * framebuffer_line_length_);
      for (int x = 0; x < kScreenWidth; ++x) {
        destination[x] = source[kScreenWidth - 1 - x];
      }
    }

    fb_var_screeninfo variable{};
    if (ioctl(framebuffer_fd_, FBIOGET_VSCREENINFO, &variable) != 0) {
      error_ = std::string("Could not inspect the device framebuffer: ") +
               std::strerror(errno);
      return false;
    }
    variable.yoffset = framebuffer_next_yoffset_;
    variable.activate = FB_ACTIVATE_NOW;
    if (ioctl(framebuffer_fd_, FBIOPAN_DISPLAY, &variable) != 0) {
      error_ = std::string("Could not present the device framebuffer: ") +
               std::strerror(errno);
      return false;
    }
    framebuffer_next_yoffset_ =
        framebuffer_next_yoffset_ == 0 ? framebuffer_page_height_ : 0;
    return true;
  }
#endif
  SDL_Surface* upload_surface = frame_surface_;
  if (presentation_surface_) {
    if (SDL_BlitScaled(frame_surface_, nullptr, presentation_surface_, nullptr) !=
        0) {
      error_ = std::string("Could not scale the Mini presentation frame: ") +
               SDL_GetError();
      return false;
    }
    upload_surface = presentation_surface_;
  }
  if (SDL_UpdateTexture(frame_texture_, nullptr, upload_surface->pixels,
                        upload_surface->pitch) != 0) {
    error_ = std::string("Could not upload the device frame: ") + SDL_GetError();
    return false;
  }
#ifdef MIYONOS_ENABLE_SIMULATOR
  if (simulator_) {
    simulator_->draw(presenter_, frame_texture_);
    return true;
  }
#endif
  SDL_SetRenderDrawColor(presenter_, 0, 0, 0, 255);
  SDL_RenderClear(presenter_);
  if (SDL_RenderCopy(presenter_, frame_texture_, nullptr, nullptr) != 0) {
    error_ = std::string("Could not copy the device frame: ") + SDL_GetError();
    return false;
  }
  SDL_RenderPresent(presenter_);
  return true;
}

void FramePresenter::show_onion_home(uint32_t duration_ms) {
#ifdef MIYONOS_ENABLE_SIMULATOR
  if (!simulator_) return;
  const uint32_t until = SDL_GetTicks() + duration_ms;
  bool running = true;
  while (running && SDL_GetTicks() < until) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN ||
          event.type == SDL_MOUSEBUTTONDOWN) {
        running = false;
      }
    }
    simulator_->draw(presenter_, nullptr, true);
    SDL_Delay(33);
  }
#else
  (void)duration_ms;
#endif
}

#ifdef MIYONOS_ENABLE_SIMULATOR
bool FramePresenter::capture_bmp(const std::string& path) const {
  if (!frame_surface_) return false;
  return SDL_SaveBMP(frame_surface_, path.c_str()) == 0;
}

bool FramePresenter::capture_presented_bmp(const std::string& path) const {
  if (!presenter_) return false;
  int width = 0;
  int height = 0;
  if (SDL_GetRendererOutputSize(presenter_, &width, &height) != 0 ||
      width <= 0 || height <= 0) {
    return false;
  }
  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
      0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!surface) return false;
  const bool read_ok = SDL_RenderReadPixels(
                           presenter_, nullptr, SDL_PIXELFORMAT_ARGB8888,
                           surface->pixels, surface->pitch) == 0;
  const bool saved = read_ok && SDL_SaveBMP(surface, path.c_str()) == 0;
  SDL_FreeSurface(surface);
  return saved;
}
#endif

}  // namespace miyonos
