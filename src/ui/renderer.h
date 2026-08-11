#pragma once

#include <SDL.h>

#include <cstdint>
#include <map>
#include <string>

#include "app/controller.h"
#include "ui/bitmap_font.h"

namespace miyonos {

class Renderer {
 public:
  explicit Renderer(SDL_Renderer* renderer);
  ~Renderer();
  void draw(const ViewState& view, const Settings& settings);

 private:
  void background();
  void status_bar(const ViewState& view, const std::string& title);
  void hints(const std::string& left, const std::string& right);
  void toast(const ViewState& view);
  void splash(const ViewState& view);
  void discovery(const ViewState& view);
  void now_playing(const ViewState& view, const Settings& settings);
  void rooms(const ViewState& view, bool editor);
  void speakers(const ViewState& view);
  void media_list(const ViewState& view, const std::vector<BrowseItem>& items,
                  const std::string& title);
  void queue_list(const ViewState& view);
  void favorites(const ViewState& view);
  void playlists(const ViewState& view);
  void menu(const ViewState& view);
  void settings(const ViewState& view, const Settings& settings);
  void button_mapping(const ViewState& view);
  void ip_editor(const ViewState& view);
  void help(const ViewState& view);
  void controls_overlay(const ViewState& view, const Settings& settings);
  void idle_battery_saver_overlay();
  void about(const ViewState& view);
  void diagnostics(const ViewState& view);
  void offline(const ViewState& view);
  void confirm_exit(const ViewState& view);
  void confirm_action(const ViewState& view);
  void list_rows(const std::vector<std::string>& primary,
                 const std::vector<std::string>& secondary, int selection,
                 int top_y = 62);
  void draw_fallback_artwork(const SDL_Rect& area);
  void draw_artwork(const std::string& path, const SDL_Rect& area);
  void draw_queue_thumbnail(const std::string& path, const SDL_Rect& area);
  void draw_queue_thumbnail_fallback(const SDL_Rect& area);
  void draw_speaker_model(const Player& player, const SDL_Rect& area);
  void draw_marquee(const std::string& text, int x, int y, int scale,
                    SDL_Color color, int width, uint64_t now);
  std::string setting_value(int index, const Settings& settings) const;
  void release_artwork();
  void release_queue_thumbnails();

  struct ThumbnailTexture {
    SDL_Texture* texture = nullptr;
    uint64_t last_used = 0;
  };

  SDL_Renderer* renderer_;
  BitmapFont font_;
  void* image_library_ = nullptr;
  SDL_Surface* (*image_load_)(const char*) = nullptr;
  SDL_Texture* artwork_texture_ = nullptr;
  std::string artwork_path_;
  int artwork_width_ = 0;
  int artwork_height_ = 0;
  std::map<std::string, ThumbnailTexture> queue_thumbnails_;
  uint64_t queue_thumbnail_tick_ = 0;
};

}  // namespace miyonos
