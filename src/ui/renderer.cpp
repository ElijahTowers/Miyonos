#include "ui/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <sstream>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

#include "platform/clock.h"
#include "platform/battery.h"
#include "platform/logger.h"
#include "sonos/protocol.h"
#include "ui/strings.h"

namespace miyonos {

namespace {

constexpr SDL_Color kNavy{7, 20, 43, 255};
constexpr SDL_Color kPanel{15, 39, 70, 255};
constexpr SDL_Color kPanelLight{24, 58, 93, 255};
constexpr SDL_Color kCream{255, 241, 207, 255};
constexpr SDL_Color kMuted{151, 172, 184, 255};
constexpr SDL_Color kMint{113, 214, 177, 255};
constexpr SDL_Color kCoral{255, 115, 84, 255};
constexpr SDL_Color kDark{7, 29, 59, 255};
constexpr SDL_Color kWarning{255, 194, 92, 255};

void fill(SDL_Renderer* renderer, const SDL_Rect& rectangle, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer, &rectangle);
}

std::string yes_no(bool value) { return value ? "On" : "Off"; }

std::string state_name(TransportState state) {
  switch (state) {
    case TransportState::Playing: return "Playing";
    case TransportState::Paused: return "Paused";
    case TransportState::Stopped: return "Stopped";
    case TransportState::Transitioning: return "Loading";
    case TransportState::NoMedia: return "No media";
    default: return "Unknown";
  }
}

std::string clipped(const std::string& text, std::size_t maximum) {
  if (text.size() <= maximum) return text;
  if (maximum < 4) return text.substr(0, maximum);
  return text.substr(0, maximum - 3) + "...";
}

uint64_t visual_clock_ms() {
  const char* fixed = std::getenv("MIYONOS_SCREENSHOT_TIME_MS");
  if (fixed && fixed[0] != '\0') {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(fixed, &end, 10);
    if (end && *end == '\0') return static_cast<uint64_t>(value);
  }
  return monotonic_ms();
}

#ifdef __APPLE__
SDL_Surface* load_with_image_io(const char* path) {
  if (!path || path[0] == '\0') return nullptr;
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(path),
      static_cast<CFIndex>(std::strlen(path)), false);
  if (!url) return nullptr;
  CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
  CFRelease(url);
  if (!source) return nullptr;
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (!image) return nullptr;

  const std::size_t width = CGImageGetWidth(image);
  const std::size_t height = CGImageGetHeight(image);
  if (width == 0 || height == 0 || width > 2048 || height > 2048 ||
      width * height > 4ULL * 1024 * 1024) {
    CGImageRelease(image);
    return nullptr;
  }

  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
      0, static_cast<int>(width), static_cast<int>(height), 32,
      SDL_PIXELFORMAT_RGBA32);
  if (!surface) {
    CGImageRelease(image);
    return nullptr;
  }
  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      surface->pixels, width, height, 8, static_cast<std::size_t>(surface->pitch),
      color_space,
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGColorSpaceRelease(color_space);
  if (!context) {
    SDL_FreeSurface(surface);
    CGImageRelease(image);
    return nullptr;
  }
  CGContextDrawImage(
      context,
      CGRectMake(0, 0, static_cast<CGFloat>(width),
                 static_cast<CGFloat>(height)),
      image);
  CGContextRelease(context);
  CGImageRelease(image);
  return surface;
}
#endif

}  // namespace

Renderer::Renderer(SDL_Renderer* renderer) : renderer_(renderer), font_(renderer) {
  const bool disable_image_decoder =
      std::getenv("MIYONOS_DISABLE_IMAGE_DECODER") != nullptr;
  const char* names[] = {
#ifdef __APPLE__
      "libSDL2_image.dylib",
      "/usr/local/lib/libSDL2_image.dylib",
#else
      "libSDL2_image-2.0.so.0",
      "libSDL2_image.so",
#endif
  };
  if (!disable_image_decoder) {
    for (const char* name : names) {
      image_library_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
      if (image_library_) break;
    }
    if (image_library_) {
      image_load_ = reinterpret_cast<SDL_Surface* (*)(const char*)>(
          dlsym(image_library_, "IMG_Load"));
    }
  }
#ifdef __APPLE__
  if (!image_load_ && !disable_image_decoder) {
    image_load_ = &load_with_image_io;
    MIYONOS_LOG("artwork", "Using the built-in macOS image decoder");
  }
#endif
  if (!image_load_) {
    MIYONOS_WARN("artwork",
                 "SDL2_image could not be loaded; using fallback artwork");
  }
}

Renderer::~Renderer() {
  release_artwork();
  release_queue_thumbnails();
  if (image_library_) dlclose(image_library_);
}

void Renderer::release_artwork() {
  if (artwork_texture_) SDL_DestroyTexture(artwork_texture_);
  artwork_texture_ = nullptr;
  artwork_path_.clear();
  artwork_width_ = artwork_height_ = 0;
}

void Renderer::release_queue_thumbnails() {
  for (auto& [path, thumbnail] : queue_thumbnails_) {
    (void)path;
    if (thumbnail.texture) SDL_DestroyTexture(thumbnail.texture);
  }
  queue_thumbnails_.clear();
}

void Renderer::background() {
  SDL_SetRenderDrawColor(renderer_, kNavy.r, kNavy.g, kNavy.b, 255);
  SDL_RenderClear(renderer_);
  for (int y = 31; y < 480; y += 16) {
    SDL_SetRenderDrawColor(renderer_, 10, 27, 51, 255);
    SDL_RenderDrawLine(renderer_, 0, y, 640, y);
  }
}

void Renderer::status_bar(const ViewState& view, const std::string& title) {
  fill(renderer_, SDL_Rect{0, 0, 640, 34}, kPanel);
  font_.draw(clipped(title, 30), 16, 8, 2, kCream, 425);
  if (view.screen == Screen::NowPlaying) {
    const BatteryStatus battery = device_battery_status();
    if (battery.available()) {
      const SDL_Color color = battery.percent <= 15
                                  ? kCoral
                                  : battery.percent <= 30 ? kWarning : kMint;
      font_.draw("BAT " + std::to_string(battery.percent) + "%", 450, 13,
                 1, kMuted, 70);
      SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
      const SDL_Rect outline{527, 10, 18, 13};
      SDL_RenderDrawRect(renderer_, &outline);
      fill(renderer_, SDL_Rect{545, 14, 3, 5}, color);
      const int fill_width = battery.percent == 0
                                 ? 0
                                 : std::max(1, battery.percent * 14 / 100);
      if (fill_width) fill(renderer_, SDL_Rect{529, 12, fill_width, 9}, color);
    }
  }
  SDL_Color status = view.connected ? kMint : view.discovering ? kWarning : kCoral;
  fill(renderer_, SDL_Rect{555, 10, 8, 14}, status);
  fill(renderer_, SDL_Rect{568, 7, 8, 17}, status);
  fill(renderer_, SDL_Rect{581, 4, 8, 20}, status);
  font_.draw("LAN", 596, 10, 1, kMuted);
}

void Renderer::hints(const std::string& left, const std::string& right) {
  fill(renderer_, SDL_Rect{0, 447, 640, 33}, kPanel);
  font_.draw(left, 14, 458, 1, kMuted, 300);
  const int width = font_.width(right, 1);
  font_.draw(right, 626 - width, 458, 1, kMuted, 300);
}

void Renderer::toast(const ViewState& view) {
  if (view.toast.empty()) return;
  const int width =
      std::min(600, std::max(180, font_.width(view.toast, 1) + 36));
  fill(renderer_, SDL_Rect{320 - width / 2, 407, width, 28}, kCream);
  font_.draw_centered(clipped(view.toast, 82), 417, 1, kDark);
}

void Renderer::splash(const ViewState& view) {
  (void)view;
  // Original code-native companion mark to the packaged raster icon.
  fill(renderer_, SDL_Rect{236, 82, 168, 210}, kCream);
  fill(renderer_, SDL_Rect{252, 99, 136, 104}, kPanel);
  fill(renderer_, SDL_Rect{265, 112, 110, 78}, kNavy);
  fill(renderer_, SDL_Rect{284, 229, 54, 16}, kDark);
  fill(renderer_, SDL_Rect{303, 210, 16, 54}, kDark);
  fill(renderer_, SDL_Rect{349, 224, 16, 16}, kCoral);
  fill(renderer_, SDL_Rect{371, 244, 16, 16}, kCoral);
  fill(renderer_, SDL_Rect{349, 264, 16, 16}, kCoral);
  fill(renderer_, SDL_Rect{327, 244, 16, 16}, kCoral);
  fill(renderer_, SDL_Rect{302, 135, 12, 38}, kCoral);
  fill(renderer_, SDL_Rect{314, 135, 36, 10}, kCoral);
  fill(renderer_, SDL_Rect{338, 143, 12, 34}, kCoral);
  fill(renderer_, SDL_Rect{290, 165, 24, 18}, kCoral);
  fill(renderer_, SDL_Rect{326, 169, 24, 18}, kCoral);
  font_.draw_centered(strings::kAppName, 318, 5, kCream);
  font_.draw_centered(std::string("Version ") + MIYONOS_VERSION, 365, 2, kMint);
  font_.draw_centered(strings::kTechnicalPreview, 397, 1, kMuted);
  font_.draw_centered("Starting Miyonos...", 431, 1, kCream);
}

void Renderer::discovery(const ViewState& view) {
  status_bar(view, strings::kAppName);
  const uint64_t phase = (visual_clock_ms() / 160) % 8;
  for (int i = 0; i < 8; ++i) {
    const double angle = static_cast<double>(i) * 3.1415926535 / 4.0;
    const int x = 314 + static_cast<int>(std::cos(angle) * 54);
    const int y = 184 + static_cast<int>(std::sin(angle) * 54);
    fill(renderer_, SDL_Rect{x, y, 12, 12},
         i == static_cast<int>(phase) ? kCoral : kPanelLight);
  }
  font_.draw_centered(view.status, 275, 2, kCream);
  font_.draw_centered("Searching the local Wi-Fi network", 320, 1, kMuted);
  font_.draw_centered("No cloud account or internet connection is required",
                      346, 1, kMuted);
  hints("B  Back", "SELECT  Controls");
}

void Renderer::draw_fallback_artwork(const SDL_Rect& area) {
  fill(renderer_, area, kPanel);
  fill(renderer_, SDL_Rect{area.x + 12, area.y + 12, area.w - 24, area.h - 24},
       kPanelLight);
  const int center_x = area.x + area.w / 2;
  const int center_y = area.y + area.h / 2;
  fill(renderer_, SDL_Rect{center_x - 14, center_y - 67, 18, 88}, kMint);
  fill(renderer_, SDL_Rect{center_x + 4, center_y - 67, 54, 15}, kMint);
  fill(renderer_, SDL_Rect{center_x + 40, center_y - 57, 18, 70}, kMint);
  fill(renderer_, SDL_Rect{center_x - 36, center_y + 5, 40, 30}, kMint);
  fill(renderer_, SDL_Rect{center_x + 18, center_y - 3, 40, 30}, kMint);
  for (int i = 0; i < 5; ++i) {
    fill(renderer_, SDL_Rect{area.x + 35 + i * 37, area.y + area.h - 46,
                             8, 12 + (i % 3) * 8},
         kCoral);
  }
}

void Renderer::draw_artwork(const std::string& path, const SDL_Rect& area) {
  if (path.empty() || !image_load_) {
    draw_fallback_artwork(area);
    return;
  }
  if (path != artwork_path_) {
    release_artwork();
    artwork_path_ = path;
    SDL_Surface* surface = image_load_(path.c_str());
    if (surface && surface->w > 0 && surface->h > 0 && surface->w <= 2048 &&
        surface->h <= 2048 &&
        static_cast<uint64_t>(surface->w) * surface->h <= 4ULL * 1024 * 1024) {
      artwork_texture_ = SDL_CreateTextureFromSurface(renderer_, surface);
      artwork_width_ = surface->w;
      artwork_height_ = surface->h;
    }
    if (!artwork_texture_) {
      MIYONOS_WARN(
          "artwork",
          "Cover image could not be decoded; using fallback artwork");
    }
    if (surface) SDL_FreeSurface(surface);
  }
  if (!artwork_texture_) {
    draw_fallback_artwork(area);
    return;
  }
  const double scale = std::min(static_cast<double>(area.w) / artwork_width_,
                                static_cast<double>(area.h) / artwork_height_);
  SDL_Rect destination{
      area.x + (area.w - static_cast<int>(artwork_width_ * scale)) / 2,
      area.y + (area.h - static_cast<int>(artwork_height_ * scale)) / 2,
      static_cast<int>(artwork_width_ * scale),
      static_cast<int>(artwork_height_ * scale)};
  fill(renderer_, area, kPanel);
  SDL_RenderCopy(renderer_, artwork_texture_, nullptr, &destination);
}

void Renderer::draw_queue_thumbnail_fallback(const SDL_Rect& area) {
  fill(renderer_, area, kPanel);
  const int inset = std::max(3, area.w / 8);
  fill(renderer_, SDL_Rect{area.x + inset, area.y + inset,
                           area.w - inset * 2, area.h - inset * 2},
       kPanelLight);
  const int note = std::max(5, area.w / 8);
  fill(renderer_, SDL_Rect{area.x + area.w / 2 - note / 2,
                           area.y + area.h / 4, note, area.h / 2},
       kMint);
  fill(renderer_, SDL_Rect{area.x + area.w / 2 - note / 2,
                           area.y + area.h / 4, area.w / 4, note},
       kMint);
  fill(renderer_, SDL_Rect{area.x + area.w / 2 - note,
                           area.y + area.h * 3 / 5, note * 2, note},
       kCoral);
}

void Renderer::draw_queue_thumbnail(const std::string& path,
                                    const SDL_Rect& area) {
  if (path.empty() || !image_load_) {
    draw_queue_thumbnail_fallback(area);
    return;
  }

  auto found = queue_thumbnails_.find(path);
  if (found == queue_thumbnails_.end()) {
    SDL_Surface* source = image_load_(path.c_str());
    SDL_Texture* texture = nullptr;
    if (source && source->w > 0 && source->h > 0 && source->w <= 2048 &&
        source->h <= 2048 &&
        static_cast<uint64_t>(source->w) * source->h <= 4ULL * 1024 * 1024) {
      // Keep the on-device thumbnail cache deliberately tiny. Storing full
      // album-size textures for six rows would use too much memory on Miyoo.
      constexpr int kThumbnailPixels = 96;
      SDL_Surface* thumbnail = SDL_CreateRGBSurfaceWithFormat(
          0, kThumbnailPixels, kThumbnailPixels, 32, SDL_PIXELFORMAT_RGBA32);
      if (thumbnail) {
        SDL_FillRect(thumbnail, nullptr,
                     SDL_MapRGBA(thumbnail->format, kPanel.r, kPanel.g,
                                 kPanel.b, kPanel.a));
        const double scale = std::min(
            static_cast<double>(kThumbnailPixels) / source->w,
            static_cast<double>(kThumbnailPixels) / source->h);
        const int width = std::max(1, static_cast<int>(source->w * scale));
        const int height = std::max(1, static_cast<int>(source->h * scale));
        SDL_Rect destination{(kThumbnailPixels - width) / 2,
                             (kThumbnailPixels - height) / 2, width, height};
        if (SDL_BlitScaled(source, nullptr, thumbnail, &destination) == 0) {
          texture = SDL_CreateTextureFromSurface(renderer_, thumbnail);
        }
        SDL_FreeSurface(thumbnail);
      }
    }
    if (source) SDL_FreeSurface(source);
    found = queue_thumbnails_
                .emplace(path, ThumbnailTexture{texture, 0})
                .first;
    found->second.last_used = ++queue_thumbnail_tick_;
    constexpr std::size_t kMaximumQueueThumbnails = 8;
    while (queue_thumbnails_.size() > kMaximumQueueThumbnails) {
      auto oldest = queue_thumbnails_.begin();
      for (auto entry = queue_thumbnails_.begin();
           entry != queue_thumbnails_.end(); ++entry) {
        if (entry->second.last_used < oldest->second.last_used) oldest = entry;
      }
      if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
      queue_thumbnails_.erase(oldest);
    }
  }
  found->second.last_used = ++queue_thumbnail_tick_;
  if (!found->second.texture) {
    draw_queue_thumbnail_fallback(area);
    return;
  }
  fill(renderer_, area, kPanel);
  SDL_RenderCopy(renderer_, found->second.texture, nullptr, &area);
}

void Renderer::draw_marquee(const std::string& text, int x, int y, int scale,
                            SDL_Color color, int width, uint64_t now) {
  const int text_width = font_.width(text, scale);
  if (text_width <= width) {
    font_.draw(text, x, y, scale, color, width);
    return;
  }
  const uint64_t cycle = now % 9000;
  int offset = 0;
  if (cycle > 1800 && cycle < 7200) {
    const double progress = static_cast<double>(cycle - 1800) / 5400.0;
    offset = static_cast<int>((text_width - width + 20) * progress);
  } else if (cycle >= 7200) {
    offset = text_width - width + 20;
  }
  SDL_Rect clip{x, y - 2, width, 7 * scale + 4};
  SDL_RenderSetClipRect(renderer_, &clip);
  font_.draw(text, x - offset, y, scale, color);
  SDL_RenderSetClipRect(renderer_, nullptr);
}

void Renderer::now_playing(const ViewState& view, const Settings& settings) {
  const Group* active = nullptr;
  for (const auto& group : view.topology.groups)
    if (group.id == view.active_group_id) active = &group;
  const Player* volume_target = nullptr;
  for (const auto& player : view.topology.players) {
    if (player.uuid == view.active_room_uuid) {
      volume_target = &player;
      break;
    }
  }
  const std::string room = active ? active->name : "Select a room";
  const std::string volume_room =
      view.group_volume_target
          ? "Group"
          : volume_target && !volume_target->room_name.empty()
          ? volume_target->room_name
          : "Selected speaker";
  status_bar(view, room);
  draw_artwork(view.artwork_path, SDL_Rect{20, 54, 246, 246});
  if (view.artwork_path.empty()) {
    font_.draw_centered("Cover unavailable", 282, 1, kMuted);
  }
  const Track& track = view.playback.track;
  const bool radio_station = is_radio_stream(track);
  std::string title = radio_station ? strip_radio_backend_suffix(track.title)
                                    : track.title;
  if (is_technical_media_text(title)) {
    title = !is_technical_media_text(track.station) ? track.station
                                                     : radio_station
                                                           ? "Radio station"
                                                           : strings::kNothingPlaying;
  }
  const std::string artist =
      is_technical_media_text(track.artist)
          ? radio_station ? "Live radio" : state_name(view.playback.state)
          : track.artist;
  std::string detail;
  if (!is_technical_media_text(track.album)) detail = track.album;
  else if (!is_technical_media_text(track.station) && track.station != title)
    detail = track.station;
  draw_marquee(title, 292, 66, 3, kCream, 326, visual_clock_ms());
  font_.draw(clipped(artist, 50), 292, 107, 2, kMint, 320);
  font_.draw(clipped(detail, 52), 292, 138, 1, kMuted, 320);
  const char* symbol = view.playback.state == TransportState::Playing ? ">" : "II";
  font_.draw(symbol, 292, 181, 3, kCoral);
  const std::string speaker_status =
      std::string(view.group_volume_target ? "Group: " : "Speaker: ") +
      volume_room + "  " +
      (view.speaker_volume < 0
           ? "Reading..."
           : view.speaker_muted ? "Muted"
                                 : "Volume " +
                                       std::to_string(view.speaker_volume));
  font_.draw(clipped(speaker_status, 48), 348, 189, 1,
             view.speaker_muted ? kCoral : kCream, 255);
  fill(renderer_, SDL_Rect{292, 232, 318, 10}, kPanelLight);
  const int duration = track.duration_seconds;
  const int elapsed = std::max(0, track.elapsed_seconds);
  const int progress =
      duration > 0 ? std::min(318, elapsed * 318 / std::max(1, duration)) : 0;
  if (progress) fill(renderer_, SDL_Rect{292, 232, progress, 10}, kMint);
  font_.draw(format_duration(elapsed), 292, 255, 1, kMuted);
  const std::string total = duration > 0 ? format_duration(duration) : "--:--";
  font_.draw(total, 610 - font_.width(total, 1), 255, 1, kMuted);
  const int volume_width =
      clamp_volume(std::max(0, view.speaker_volume)) * 2;
  font_.draw("VOL " + clipped(volume_room, 22), 20, 307, 1, kMuted, 235);
  fill(renderer_, SDL_Rect{55, 322, 200, 8}, kPanelLight);
  fill(renderer_, SDL_Rect{55, 322, volume_width, 8}, kCoral);
  const bool playlist_active = !view.playback.playlist_title.empty();
  int queue_y = 294;
  int group_y = 314;
  int group_width = 320;
  if (playlist_active) {
    // Keep the current-track cover prominent on the left and reserve the lower
    // right for the source playlist that owns this queue.
    font_.draw("PLAYLIST", 292, 284, 1, kMuted);
    draw_marquee(view.playback.playlist_title, 292, 299, 1, kMint, 212,
                 visual_clock_ms());
    const std::string playlist_artwork =
        view.now_playing_playlist_artwork_title ==
                view.playback.playlist_title
            ? view.now_playing_playlist_artwork_path
            : "";
    draw_queue_thumbnail(playlist_artwork, SDL_Rect{520, 284, 88, 88});
    queue_y = 320;
    group_y = 338;
    group_width = 212;
  }
  if (track.queue_position > 0) {
    font_.draw("Queue item " + std::to_string(track.queue_position), 292, queue_y,
               1, kMuted);
  }
  if (active && active->member_uuids.size() > 1) {
    font_.draw(clipped(std::to_string(active->member_uuids.size()) +
                           " rooms grouped  R1 selects next target",
                       playlist_active ? 34 : 54),
               292, group_y, 1, kMuted, group_width);
  }
  if (settings.button_hints != ButtonHints::Never) {
    hints("SELECT  Controls", "L2  Queue    R2  Favorites");
  }
}

void Renderer::list_rows(const std::vector<std::string>& primary,
                         const std::vector<std::string>& secondary,
                         int selection, int top_y) {
  constexpr int row_height = 43;
  constexpr int visible_rows = 8;
  const int count = static_cast<int>(primary.size());
  int start = std::max(0, selection - visible_rows / 2);
  start = std::min(start, std::max(0, count - visible_rows));
  for (int row = 0; row < visible_rows && start + row < count; ++row) {
    const int index = start + row;
    const int y = top_y + row * row_height;
    if (index == selection) {
      fill(renderer_, SDL_Rect{12, y - 5, 616, row_height - 2}, kCream);
    } else if (row % 2 == 0) {
      fill(renderer_, SDL_Rect{12, y - 5, 616, row_height - 2}, kPanel);
    }
    const SDL_Color main_color = index == selection ? kDark : kCream;
    const SDL_Color sub_color = index == selection ? SDL_Color{63, 82, 98, 255}
                                                   : kMuted;
    font_.draw(clipped(primary[index], 64), 24, y + 1, 2, main_color, 590);
    if (index < static_cast<int>(secondary.size())) {
      font_.draw(clipped(secondary[index], 88), 24, y + 24, 1, sub_color, 590);
    }
  }
}

void Renderer::rooms(const ViewState& view, bool editor) {
  status_bar(view, editor ? "Group Editor" : "Rooms & Groups");
  std::vector<std::string> primary;
  std::vector<std::string> secondary;
  if (editor) {
    const Group* active = nullptr;
    for (const auto& group : view.topology.groups)
      if (group.id == view.active_group_id) active = &group;
    for (const auto& player : view.topology.players) {
      if (!player.visible) continue;
      const bool joined =
          active && std::find(active->member_uuids.begin(),
                              active->member_uuids.end(),
                              player.uuid) != active->member_uuids.end();
      primary.push_back(std::string(joined ? "[+] " : "[ ] ") +
                        (player.room_name.empty() ? "Unnamed room"
                                                  : player.room_name));
      secondary.push_back(
          player.available
              ? joined ? "A: remove from active group" : "A: join active group"
              : "Unavailable");
    }
  } else {
    for (const auto& group : view.topology.groups) {
      primary.push_back((group.id == view.active_group_id ? "> " : "  ") +
                        group.name);
      std::string detail =
          std::to_string(group.member_uuids.size()) +
          (group.member_uuids.size() == 1 ? " room" : " rooms");
      if (group.id == view.active_group_id &&
          !view.playback.track.title.empty()) {
        detail += "  -  " + view.playback.track.title;
      }
      secondary.push_back(detail);
    }
  }
  if (primary.empty()) {
    font_.draw_centered("No controllable rooms", 205, 2, kCream);
    font_.draw_centered("Press SELECT to search again", 246, 1, kMuted);
  } else {
    list_rows(primary, secondary, view.selection);
  }
  hints("B  Back", editor ? "A  Toggle room" : "A  Select    X  Group editor");
}

void Renderer::draw_speaker_model(const Player& player, const SDL_Rect& area) {
  fill(renderer_, area, kPanel);
  const std::string model =
      lowercase(player.model_name + " " + player.model_number);
  const SDL_Color body = kCream;
  const SDL_Color detail = kPanelLight;
  const SDL_Color accent = kMint;

  const bool soundbar = model.find("arc") != std::string::npos ||
                        model.find("beam") != std::string::npos ||
                        model.find("playbar") != std::string::npos ||
                        model.find("playbase") != std::string::npos;
  const bool portable = model.find("roam") != std::string::npos ||
                        model.find("move") != std::string::npos;
  const bool subwoofer = model.find("sub") != std::string::npos;
  const bool headphones = model.find("ace") != std::string::npos;

  if (soundbar) {
    const SDL_Rect bar{area.x + 8, area.y + area.h / 2 - 13, area.w - 16, 26};
    fill(renderer_, bar, body);
    for (int index = 0; index < 5; ++index) {
      fill(renderer_, SDL_Rect{bar.x + 13 + index * (bar.w - 26) / 4,
                               bar.y + 10, 4, 4},
           detail);
    }
  } else if (subwoofer) {
    const SDL_Rect cabinet{area.x + area.w / 2 - 28, area.y + 7, 56, area.h - 14};
    fill(renderer_, cabinet, body);
    fill(renderer_, SDL_Rect{cabinet.x + 13, cabinet.y + 18,
                             cabinet.w - 26, cabinet.h - 36},
         detail);
    fill(renderer_, SDL_Rect{cabinet.x + 22, cabinet.y + 29,
                             cabinet.w - 44, cabinet.h - 58},
         accent);
  } else if (headphones) {
    fill(renderer_, SDL_Rect{area.x + area.w / 2 - 25, area.y + 10, 50, 10},
         body);
    fill(renderer_, SDL_Rect{area.x + 13, area.y + 30, 20, 42}, body);
    fill(renderer_, SDL_Rect{area.x + area.w - 33, area.y + 30, 20, 42}, body);
    fill(renderer_, SDL_Rect{area.x + 17, area.y + 37, 12, 28}, detail);
    fill(renderer_, SDL_Rect{area.x + area.w - 29, area.y + 37, 12, 28}, detail);
  } else {
    const int width = portable ? 52 : 64;
    const int height = portable ? area.h - 12 : area.h - 8;
    const SDL_Rect cabinet{area.x + (area.w - width) / 2,
                           area.y + (area.h - height) / 2, width, height};
    fill(renderer_, cabinet, body);
    fill(renderer_, SDL_Rect{cabinet.x + 9, cabinet.y + 13,
                             cabinet.w - 18, cabinet.h - 34},
         detail);
    fill(renderer_, SDL_Rect{cabinet.x + cabinet.w / 2 - 10,
                             cabinet.y + cabinet.h / 2 - 12, 20, 20},
         accent);
    fill(renderer_, SDL_Rect{cabinet.x + cabinet.w / 2 - 3,
                             cabinet.y + cabinet.h - 12, 6, 6},
         kCoral);
  }
}

void Renderer::speakers(const ViewState& view) {
  const Group* active = nullptr;
  for (const auto& group : view.topology.groups) {
    if (group.id == view.active_group_id) {
      active = &group;
      break;
    }
  }
  status_bar(view, "Speaker Volumes");
  if (!active) {
    font_.draw_centered("Select a room group first", 210, 2, kCream);
    hints("B  Back", "Y  Rooms & Groups");
    return;
  }

  std::vector<const Player*> speakers;
  for (const auto& uuid : active->member_uuids) {
    const auto found = std::find_if(
        view.topology.players.begin(), view.topology.players.end(),
        [&uuid](const Player& player) { return player.uuid == uuid; });
    if (found != view.topology.players.end() && found->visible &&
        found->available) {
      speakers.push_back(&*found);
    }
  }
  if (speakers.empty()) {
    font_.draw_centered("No available speakers in this group", 210, 2, kCream);
    hints("B  Back", "SELECT  Controls");
    return;
  }

  const int selection = std::max(
      0, std::min<int>(view.selection, static_cast<int>(speakers.size() - 1)));
  const int page_start = selection / 4 * 4;
  const std::string subtitle =
      clipped(active->name, 34) + "  -  " + std::to_string(speakers.size()) +
      (speakers.size() == 1 ? " speaker" : " speakers");
  font_.draw_centered(subtitle, 48, 1, kMuted);

  constexpr int kCardWidth = 296;
  constexpr int kCardHeight = 158;
  for (int card = 0; card < 4 && page_start + card < static_cast<int>(speakers.size());
       ++card) {
    const int index = page_start + card;
    const int x = 16 + (card % 2) * 312;
    const int y = 68 + (card / 2) * 170;
    const bool selected = index == selection;
    fill(renderer_, SDL_Rect{x, y, kCardWidth, kCardHeight},
         selected ? kCream : kPanel);
    if (!selected) {
      fill(renderer_, SDL_Rect{x + 2, y + 2, kCardWidth - 4, kCardHeight - 4},
           kPanelLight);
    }
    const SDL_Color primary = selected ? kDark : kCream;
    const SDL_Color secondary = selected ? SDL_Color{63, 82, 98, 255} : kMuted;
    const Player& speaker = *speakers[static_cast<std::size_t>(index)];
    const auto photo = view.speaker_product_photo_paths.find(speaker.uuid);
    const bool has_official_photo =
        photo != view.speaker_product_photo_paths.end() && !photo->second.empty();
    const SDL_Rect image_area{x + 12, y + 40, 86, 94};
    if (has_official_photo) draw_queue_thumbnail(photo->second, image_area);
    else draw_speaker_model(speaker, image_area);
    font_.draw(clipped(speaker.room_name.empty() ? "Unnamed speaker"
                                                 : speaker.room_name,
                       20),
               x + 112, y + 16, 2, primary, 168);
    const std::string model = speaker.model_name.empty()
                                  ? speaker.model_number.empty()
                                        ? "Sonos speaker"
                                        : speaker.model_number
                                  : speaker.model_name;
    font_.draw(clipped(model, 27), x + 112, y + 45, 1, secondary, 168);

    const auto volume = view.speaker_volumes.find(speaker.uuid);
    const bool known = volume != view.speaker_volumes.end();
    const int level = known ? clamp_volume(volume->second.volume) : 0;
    const bool muted = known && volume->second.muted;
    font_.draw(known ? muted ? "MUTED" : "VOL " + std::to_string(level)
                      : "READING...",
               x + 112, y + 72, 1, muted ? kCoral : primary, 160);
    fill(renderer_, SDL_Rect{x + 112, y + 93, 160, 8},
         selected ? SDL_Color{170, 183, 186, 255} : kPanel);
    if (known && level > 0) {
      fill(renderer_, SDL_Rect{x + 112, y + 93, level * 160 / 100, 8},
           muted ? kCoral : kMint);
    }
    font_.draw(selected ? has_official_photo ? "Selected - Official photo"
                                           : "Selected"
                        : has_official_photo ? "Official Sonos photo" : "",
               x + 112, y + 119, 1, secondary, 160);
  }
  if (speakers.size() > 4) {
    font_.draw_centered(std::to_string(page_start + 1) + "-" +
                            std::to_string(std::min<int>(
                                page_start + 4, static_cast<int>(speakers.size()))) +
                            " of " + std::to_string(speakers.size()),
                        420, 1, kMuted);
  }
  hints("L/R  Speaker   Up/Down  Volume", "A  Mute   X  Sync all   B  Back");
}

void Renderer::queue_list(const ViewState& view) {
  status_bar(view, "Queue");
  if (view.queue.empty()) {
    const bool failed = !view.busy && !view.error.empty();
    font_.draw_centered(
        view.busy ? "Loading..."
                  : failed ? "Could not load this list" : "Nothing here yet",
        204, 2, kCream);
    font_.draw_centered(view.busy ? view.status : "Press SELECT to retry", 246,
                        1, kMuted);
    hints("B  Back    D-Pad  Browse", "A  Play    X  Favorite playlists");
    return;
  }

  // Keep Queue aligned with the Favorites browser: the list stays readable
  // on the left and the currently selected track gets a useful, full-size
  // cover preview on the right. Queue numbers remain visible as an anchor
  // when starting from a specific track.
  constexpr int kRowHeight = 48;
  constexpr int kVisibleRows = 7;
  const int count = static_cast<int>(view.queue.size());
  int start = std::max(0, view.selection - kVisibleRows / 2);
  start = std::min(start, std::max(0, count - kVisibleRows));
  for (int row = 0; row < kVisibleRows && start + row < count; ++row) {
    const int index = start + row;
    const int y = 64 + row * kRowHeight;
    const bool selected = index == view.selection;
    if (selected) {
      fill(renderer_, SDL_Rect{12, y - 4, 380, kRowHeight - 3}, kCream);
    } else if (row % 2 == 0) {
      fill(renderer_, SDL_Rect{12, y - 4, 380, kRowHeight - 3}, kPanel);
    }
    const SDL_Color main = selected ? kDark : kCream;
    const SDL_Color sub = selected ? SDL_Color{63, 82, 98, 255} : kMuted;
    const SDL_Color number = selected ? kDark : kMint;
    if (index + 1 == view.playback.track.queue_position) {
      font_.draw(">", 18, y + 10, 1, number, 10);
    }
    font_.draw(std::to_string(index + 1), 32, y + 10, 1, number, 28);
    const BrowseItem& item = view.queue[index];
    font_.draw(clipped(item.title.empty() ? "Untitled track" : item.title, 31),
               68, y, 2, main, 304);
    std::string detail = item.artist;
    if (!item.album.empty()) {
      if (!detail.empty()) detail += " - ";
      detail += item.album;
    }
    if (item.duration_seconds > 0) {
      if (!detail.empty()) detail += "   ";
      detail += format_duration(item.duration_seconds);
    }
    font_.draw(clipped(detail, 43), 68, y + 25, 1, sub, 304);
  }

  const BrowseItem& selected = view.queue[view.selection];
  const std::string selected_artwork =
      view.selection < static_cast<int>(view.queue_artwork_paths.size())
          ? view.queue_artwork_paths[view.selection]
          : "";
  font_.draw_centered("Selected track", 75, 1, kMuted, 500);
  draw_artwork(selected_artwork, SDL_Rect{424, 96, 184, 184});
  if (selected_artwork.empty()) {
    font_.draw_centered("Cover unavailable", 291, 1, kMuted, 516);
  }
  font_.draw_centered(
      clipped(selected.title.empty() ? "Untitled track" : selected.title, 24),
      322, 1, kCream, 516);
  std::string selected_detail = selected.artist;
  if (!selected.album.empty()) {
    if (!selected_detail.empty()) selected_detail += " - ";
    selected_detail += selected.album;
  }
  font_.draw_centered(clipped(selected_detail, 36), 360, 1, kMuted, 516);
  font_.draw_centered("A starts from this track", 380, 1, kMuted, 516);
  hints("B  Back    D-Pad  Browse", "A  Play    X  Favorite playlists");
}

void Renderer::media_list(const ViewState& view,
                          const std::vector<BrowseItem>& items,
                          const std::string& title) {
  if (view.screen == Screen::Queue) {
    queue_list(view);
    return;
  }
  status_bar(view, title);
  std::vector<std::string> primary;
  std::vector<std::string> secondary;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    std::string prefix;
    if (view.screen == Screen::Queue) {
      prefix = view.playback.track.queue_position ==
                       static_cast<int>(i + 1)
                   ? "> "
                   : "  ";
      prefix += std::to_string(i + 1) + ". ";
    }
    primary.push_back(prefix +
                      (item.title.empty() ? "Untitled item" : item.title));
    std::string detail = item.container ? "Folder" : item.artist;
    if (!item.album.empty()) {
      if (!detail.empty()) detail += " - ";
      detail += item.album;
    }
    if (item.duration_seconds > 0) {
      if (!detail.empty()) detail += "   ";
      detail += format_duration(item.duration_seconds);
    }
    secondary.push_back(detail);
  }
  if (items.empty()) {
    const bool failed = !view.busy && !view.error.empty();
    font_.draw_centered(
        view.busy ? "Loading..."
                  : failed ? "Could not load this list" : "Nothing here yet",
        204, 2, kCream);
    font_.draw_centered(view.busy ? view.status : "Press SELECT to retry", 246,
                        1, kMuted);
  } else {
    list_rows(primary, secondary, view.selection);
  }
  hints("B  Back    D-Pad  Browse",
        view.screen == Screen::Queue
            ? "A  Play    X  Favorite playlists"
        : "A  Open/Play");
}

void Renderer::favorites(const ViewState& view) {
  status_bar(view, "Favorites");
  if (view.favorites.empty()) {
    const bool failed = !view.busy && !view.error.empty();
    font_.draw_centered(
        view.busy ? "Loading favorites..."
                   : failed ? "Could not load favorites" : "No favorites found",
        204, 2, kCream);
    font_.draw_centered(view.busy ? view.status : "Press SELECT to retry", 246,
                        1, kMuted);
    hints("B  Back", "SELECT  Controls");
    return;
  }

  constexpr int row_height = 48;
  constexpr int visible_rows = 7;
  const int count = static_cast<int>(view.favorites.size());
  int start = std::max(0, view.selection - visible_rows / 2);
  start = std::min(start, std::max(0, count - visible_rows));
  for (int row = 0; row < visible_rows && start + row < count; ++row) {
    const int index = start + row;
    const int y = 64 + row * row_height;
    if (index == view.selection) {
      fill(renderer_, SDL_Rect{12, y - 4, 380, row_height - 3}, kCream);
    } else if (row % 2 == 0) {
      fill(renderer_, SDL_Rect{12, y - 4, 380, row_height - 3}, kPanel);
    }
    const BrowseItem& item = view.favorites[index];
    const SDL_Color main = index == view.selection ? kDark : kCream;
    const SDL_Color sub = index == view.selection ? SDL_Color{63, 82, 98, 255}
                                                   : kMuted;
    font_.draw(clipped(item.title.empty() ? "Untitled favorite" : item.title,
                       36),
               24, y, 2, main, 350);
    std::string detail = item.container ? "Folder" : item.artist;
    if (!item.album.empty()) {
      if (!detail.empty()) detail += " - ";
      detail += item.album;
    }
    if (item.duration_seconds > 0) {
      if (!detail.empty()) detail += "   ";
      detail += format_duration(item.duration_seconds);
    }
    font_.draw(clipped(detail, 48), 24, y + 25, 1, sub, 350);
  }

  const BrowseItem& selected = view.favorites[view.selection];
  font_.draw_centered("Selected favorite", 75, 1, kMuted, 500);
  draw_artwork(view.favorite_artwork_path, SDL_Rect{424, 96, 184, 184});
  if (view.favorite_artwork_path.empty()) {
    font_.draw_centered("Cover unavailable", 291, 1, kMuted, 516);
  }
  font_.draw_centered(clipped(selected.title.empty() ? "Untitled favorite"
                                                     : selected.title,
                              24),
                      322, 1, kCream, 516);
  font_.draw_centered(selected.container ? "A opens this folder"
                                        : "A starts this favorite",
                      360, 1, kMuted, 516);
  hints("B  Back    D-Pad  Browse", "A  Open/Play");
}

void Renderer::playlists(const ViewState& view) {
  status_bar(view, "Favorite Playlists");
  if (view.playlists.empty()) {
    const bool failed = !view.busy && !view.error.empty();
    font_.draw_centered(
        view.busy ? "Loading favorite playlists..."
                   : failed ? "Could not load favorite playlists"
                            : "No favorite playlists found",
        204, 2, kCream);
    font_.draw_centered(view.busy ? view.status : "Press SELECT to retry", 246,
                        1, kMuted);
    hints("B  Back", "X  Current queue");
    return;
  }

  constexpr int row_height = 48;
  constexpr int visible_rows = 7;
  const int count = static_cast<int>(view.playlists.size());
  int start = std::max(0, view.selection - visible_rows / 2);
  start = std::min(start, std::max(0, count - visible_rows));
  for (int row = 0; row < visible_rows && start + row < count; ++row) {
    const int index = start + row;
    const int y = 64 + row * row_height;
    if (index == view.selection) {
      fill(renderer_, SDL_Rect{12, y - 4, 380, row_height - 3}, kCream);
    } else if (row % 2 == 0) {
      fill(renderer_, SDL_Rect{12, y - 4, 380, row_height - 3}, kPanel);
    }
    const BrowseItem& item = view.playlists[index];
    const SDL_Color main = index == view.selection ? kDark : kCream;
    const SDL_Color sub = index == view.selection ? SDL_Color{63, 82, 98, 255}
                                                   : kMuted;
    font_.draw(clipped(item.title.empty() ? "Untitled playlist" : item.title,
                       36),
               24, y, 2, main, 350);
    const std::string detail = item.album.empty() ? "Sonos favorite playlist"
                                                   : item.album;
    font_.draw(clipped(detail, 48), 24, y + 25, 1, sub, 350);
  }

  const BrowseItem& selected = view.playlists[view.selection];
  font_.draw_centered("Selected playlist", 75, 1, kMuted, 500);
  draw_artwork(view.playlist_artwork_path, SDL_Rect{424, 96, 184, 184});
  if (view.playlist_artwork_path.empty()) {
    font_.draw_centered("Cover unavailable", 291, 1, kMuted, 516);
  }
  font_.draw_centered(clipped(selected.title.empty() ? "Untitled playlist"
                                                    : selected.title,
                              24),
                      322, 1, kCream, 516);
  font_.draw_centered("Playing replaces the current queue", 360, 1, kMuted,
                      516);
  font_.draw_centered("and starts from track 1", 380, 1, kMuted, 516);
  hints("B  Back    D-Pad  Browse", "A  Play    X  Current queue");
}

void Renderer::menu(const ViewState& view) {
  status_bar(view, "Main Menu");
  std::vector<std::string> names(strings::kMainMenu.begin(),
                                 strings::kMainMenu.end());
  std::vector<std::string> descriptions = {
      "Choose a room or edit groups",
      "Compare speakers and adjust individual volumes",
      "Browse and start the active queue", "Browse and start Sonos favorites",
      "Change Miyonos behavior", "Controls and connection help",
      "Version, license, and disclaimer", "Local connection and cache details"};
  list_rows(names, descriptions, view.selection);
  hints("B  Back", "A  Open");
}

std::string Renderer::setting_value(int index, const Settings& settings) const {
  switch (index) {
    case 0:
      return settings.startup_mode == StartupMode::LastUsed
                 ? "Last used"
                 : settings.startup_mode == StartupMode::SpecificRoom
                       ? "Specific room"
                       : "Ask every time";
    case 1: return std::to_string(settings.volume_step);
    case 2: return std::to_string(settings.seek_seconds) + " seconds";
    case 3: return std::to_string(settings.artwork_cache_mb) + " MB";
    case 4: return yes_no(settings.auto_artwork);
    case 5: return yes_no(settings.spotify_https_artwork);
    case 6: return yes_no(settings.official_sonos_product_photos);
    case 7:
      return settings.polling == PollingIntensity::BatterySaver
                 ? "Battery saver"
                 : settings.polling == PollingIntensity::Responsive
                       ? "Responsive"
                       : "Balanced";
    case 8:
      return settings.dim_timeout_seconds == 0
                 ? "Never"
                 : std::to_string(settings.dim_timeout_seconds) + " seconds";
    case 9: return yes_no(settings.prevent_sleep);
    case 10:
      return settings.manual_ips.empty()
                 ? "None"
                 : std::to_string(settings.manual_ips.size()) + " saved";
    case 11:
      return settings.button_hints == ButtonHints::Always
                 ? "Always"
                 : settings.button_hints == ButtonHints::Never ? "Never"
                                                                : "Briefly";
    case 12: return yes_no(settings.confirm_exit);
    case 13: return "Open";
    case 14: return "Press A";
    case 15: return "Press A";
    case 16: return "Press A";
    case 17: return yes_no(settings.diagnostics_mode);
    default: return {};
  }
}

void Renderer::settings(const ViewState& view, const Settings& settings_value) {
  status_bar(view, "Settings");
  std::vector<std::string> names(strings::kSettings.begin(),
                                 strings::kSettings.end());
  std::vector<std::string> values;
  for (int i = 0; i < static_cast<int>(names.size()); ++i) {
    values.push_back(setting_value(i, settings_value));
  }
  list_rows(names, values, view.selection);
  hints("B  Back    Left/Right  Change", "A  Change/Open");
}

void Renderer::button_mapping(const ViewState& view) {
  status_bar(view, "Button Mapping");
  std::vector<std::string> buttons;
  std::vector<std::string> actions;
  buttons.reserve(kPhysicalButtonCount);
  actions.reserve(kPhysicalButtonCount);
  for (std::size_t index = 0; index < kPhysicalButtonCount; ++index) {
    buttons.emplace_back(
        physical_button_name(static_cast<PhysicalButton>(index)));
    actions.emplace_back(action_name(view.pending_button_mapping[index]));
  }
  list_rows(buttons, actions, view.selection);
  hints("B Save   X Default", "Hold MENU+START  Restore all");
}

void Renderer::ip_editor(const ViewState& view) {
  status_bar(view, "Enter Player IP");
  font_.draw_centered("Use Left/Right to choose a number", 108, 1, kMuted);
  font_.draw_centered("Use Up/Down to change it", 132, 1, kMuted);
  int x = 108;
  for (int i = 0; i < 4; ++i) {
    const std::string octet = std::to_string(view.ip_octets[i]);
    if (i == view.ip_octet)
      fill(renderer_, SDL_Rect{x - 10, 205, 104, 58}, kCream);
    font_.draw(octet, x, 222, 3, i == view.ip_octet ? kDark : kCream);
    x += 123;
    if (i < 3) font_.draw(".", x - 31, 224, 3, kMint);
  }
  font_.draw_centered("The address is stored only on this SD card", 310, 1,
                      kMuted);
  hints("B  Cancel", "A  Save and search");
}

void Renderer::help(const ViewState& view) {
  status_bar(view, "Help");
  font_.draw("Default controls", 24, 62, 3, kCream);
  const std::vector<std::string> lines = {
      "A  Play, pause, open, or confirm",
      "B  Back or cancel",
      "D-Pad Up/Down  Selected target volume or list selection",
      "D-Pad Left/Right  Previous/next track or page",
      "X  Mute, sync speakers, or switch Queue/playlists",
      "Y  Rooms & Groups",
      "L1  Speaker Volumes     R1  Next volume target",
      "L2/R2  Queue/favorites",
      "START  Main Menu     SELECT  Controls",
      "MENU  Exit confirmation",
      "MENU + START (hold 3 sec)  Restore buttons",
      "Custom layout  Settings > Button Mapping"};
  for (std::size_t i = 0; i < lines.size(); ++i)
    font_.draw(lines[i], 26, 112 + static_cast<int>(i) * 28, 1,
               i < 2 ? kMint : kCream);
  hints("B  Back", "No touch screen required");
}

void Renderer::controls_overlay(const ViewState& view,
                                const Settings& settings) {
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  fill(renderer_, SDL_Rect{0, 0, 640, 480}, SDL_Color{0, 0, 0, 168});
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
  fill(renderer_, SDL_Rect{30, 38, 580, 404}, kCream);
  fill(renderer_, SDL_Rect{40, 48, 560, 384}, kNavy);
  font_.draw_centered("Controls", 67, 3, kCream);
  font_.draw_centered("Your current button layout", 101, 1, kMuted);

  constexpr std::size_t kRowsPerColumn = 8;
  constexpr int kLeftX = 62;
  constexpr int kRightX = 330;
  constexpr int kFirstY = 132;
  constexpr int kRowHeight = 28;
  for (std::size_t index = 0; index < kPhysicalButtonCount; ++index) {
    const bool right_column = index >= kRowsPerColumn;
    const std::size_t row = right_column ? index - kRowsPerColumn : index;
    const int x = right_column ? kRightX : kLeftX;
    const int y = kFirstY + static_cast<int>(row) * kRowHeight;
    const auto button = static_cast<PhysicalButton>(index);
    font_.draw(physical_button_name(button), x, y, 1, kMint, 98);
    font_.draw(clipped(action_name(settings.button_mapping[index]), 25),
               x + 108, y, 1, kCream, 150);
  }
  const auto controls = std::find(settings.button_mapping.begin(),
                                  settings.button_mapping.end(),
                                  Action::Controls);
  const bool has_controls_button = controls != settings.button_mapping.end();
  const std::string controls_button = has_controls_button
      ? physical_button_name(static_cast<PhysicalButton>(
            std::distance(settings.button_mapping.begin(), controls)))
      : "a mapped button";
  font_.draw_centered(has_controls_button
                          ? controls_button + " opens this guide"
                          : "Assign Show Controls in Button Mapping",
                      370, 1, kMuted);
  font_.draw_centered("A, B, or " + controls_button + " closes it", 398, 1,
                      kMint);
  (void)view;
}

void Renderer::about(const ViewState& view) {
  status_bar(view, "About");
  font_.draw_centered(strings::kAppName, 74, 5, kCream);
  font_.draw_centered(std::string("Version ") + MIYONOS_VERSION + " - " +
                          strings::kTechnicalPreview,
                      124, 1, kMint);
  font_.draw_centered("A dedicated local Sonos remote for OnionOS", 166, 1,
                      kCream);
  font_.draw_centered("No account, analytics, ads, or cloud backend", 194, 1,
                      kMuted);
  font_.draw_centered("Open source under the MIT License", 235, 1, kCream);
  font_.draw_centered("Independent community project", 291, 2, kCoral);
  font_.draw_centered("Not affiliated with or endorsed by Sonos, Inc.", 326, 1,
                      kMuted);
  font_.draw_centered("or the OnionOS project.", 348, 1, kMuted);
  hints("B  Back", "Miyonos");
}

void Renderer::diagnostics(const ViewState& view) {
  status_bar(view, "Diagnostics");
  const auto& data = view.diagnostics;
  std::vector<std::string> labels = {
      "Miyonos", "OnionOS", "Local IP", "Players", "Selected room",
      "Last refresh", "Last error", "Artwork cache", "Protocol",
      "Last SDL key"};
  std::vector<std::string> values = {
      data.version, data.onion_version, data.local_ip,
      std::to_string(data.player_count), data.selected_coordinator,
      data.last_success.empty() ? "Never" : data.last_success,
      data.last_error.empty() ? "None" : data.last_error,
      std::to_string(data.cache_bytes / 1024) + " KB", data.protocol_version,
      data.last_input_code == 0 ? "Press a button in diagnostics mode"
                                : std::to_string(data.last_input_code)};
  for (std::size_t i = 0; i < labels.size(); ++i) {
    font_.draw(labels[i], 24, 49 + static_cast<int>(i) * 28, 1, kMuted);
    font_.draw(clipped(values[i], 48), 218, 47 + static_cast<int>(i) * 28, 2,
               kCream, 400);
  }
  std::vector<std::string> actions(strings::kDiagnosticActions.begin(),
                                   strings::kDiagnosticActions.end());
  for (int i = 0; i < 3; ++i) {
    const int y = 345 + i * 30;
    if (i == view.selection)
      fill(renderer_, SDL_Rect{20, y - 6, 600, 26}, kCream);
    font_.draw(actions[i], 32, y, 1, i == view.selection ? kDark : kMint);
  }
  hints("B  Back", "A  Run action");
}

void Renderer::offline(const ViewState& view) {
  status_bar(view, "Miyonos Offline");
  font_.draw_centered("No Sonos system was found", 90, 3, kCream);
  font_.draw_centered("on this Wi-Fi network.", 123, 3, kCream);
  font_.draw_centered(
      clipped(view.error.empty() ? "Check Wi-Fi or enter a player IP."
                                 : view.error,
              78),
      186, 1, kMuted);
  std::vector<std::string> actions(strings::kOfflineActions.begin(),
                                   strings::kOfflineActions.end());
  std::vector<std::string> detail = {
      "Run local discovery again", "Useful when multicast is blocked",
      "Open connection troubleshooting"};
  list_rows(actions, detail, view.selection, 245);
  hints("D-Pad  Choose", "A  Continue");
}

void Renderer::confirm_exit(const ViewState& view) {
  now_playing(view, Settings{});
  fill(renderer_, SDL_Rect{92, 132, 456, 184}, kCream);
  fill(renderer_, SDL_Rect{101, 141, 438, 166}, kNavy);
  font_.draw_centered("Exit Miyonos?", 171, 3, kCream);
  font_.draw_centered("Playback on Sonos will continue.", 222, 1, kMuted);
  font_.draw_centered("A  Exit       B  Stay", 267, 2, kMint);
}

void Renderer::confirm_action(const ViewState& view) {
  status_bar(view, "Confirmation");
  fill(renderer_, SDL_Rect{72, 125, 496, 205}, kCream);
  fill(renderer_, SDL_Rect{82, 135, 476, 185}, kNavy);
  font_.draw_centered(clipped(view.confirmation_title, 38), 169, 3, kCream);
  font_.draw_centered(clipped(view.confirmation_message, 70), 226, 1, kMuted);
  font_.draw_centered("A  Confirm       B  Cancel", 277, 2, kMint);
}

void Renderer::draw(const ViewState& view, const Settings& settings_value) {
  background();
  switch (view.screen) {
    case Screen::Splash: splash(view); break;
    case Screen::Discovery: discovery(view); break;
    case Screen::NowPlaying: now_playing(view, settings_value); break;
    case Screen::Rooms: rooms(view, false); break;
    case Screen::GroupEditor: rooms(view, true); break;
    case Screen::Speakers: speakers(view); break;
    case Screen::Queue:
      media_list(view, view.queue, "Queue");
      break;
    case Screen::Favorites:
      favorites(view);
      break;
    case Screen::Playlists:
      playlists(view);
      break;
    case Screen::Menu: menu(view); break;
    case Screen::Settings: settings(view, settings_value); break;
    case Screen::ButtonMapping: button_mapping(view); break;
    case Screen::IpEditor: ip_editor(view); break;
    case Screen::Help: help(view); break;
    case Screen::About: about(view); break;
    case Screen::Diagnostics: diagnostics(view); break;
    case Screen::ConfirmAction: confirm_action(view); break;
    case Screen::ConfirmExit: confirm_exit(view); break;
    case Screen::Offline: offline(view); break;
  }
  toast(view);
  if (view.controls_overlay) controls_overlay(view, settings_value);
  if (settings_value.dim_timeout_seconds > 0 &&
      monotonic_ms() - view.last_input_ms >
          static_cast<uint64_t>(settings_value.dim_timeout_seconds) * 1000) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill(renderer_, SDL_Rect{0, 0, 640, 480}, SDL_Color{0, 0, 0, 155});
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
  }
  SDL_RenderPresent(renderer_);
}

}  // namespace miyonos
