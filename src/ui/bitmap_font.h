#pragma once

#include <SDL.h>

#include <string>

namespace miyonos {

class BitmapFont {
 public:
  explicit BitmapFont(SDL_Renderer* renderer) : renderer_(renderer) {}
  int width(const std::string& text, int scale) const;
  void draw(const std::string& text, int x, int y, int scale, SDL_Color color,
            int max_width = -1) const;
  void draw_centered(const std::string& text, int y, int scale, SDL_Color color,
                     int center_x = 320) const;

 private:
  const char* glyph(char character) const;
  SDL_Renderer* renderer_;
};

}  // namespace miyonos
