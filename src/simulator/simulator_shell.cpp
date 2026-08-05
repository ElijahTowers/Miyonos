#include "simulator/simulator_shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "ui/bitmap_font.h"

namespace miyonos {

namespace {

constexpr SDL_Color kDesk{22, 27, 34, 255};
constexpr SDL_Color kBody{224, 216, 199, 255};
constexpr SDL_Color kBodyShadow{118, 113, 106, 255};
constexpr SDL_Color kBezel{20, 22, 26, 255};
constexpr SDL_Color kButton{47, 50, 57, 255};
constexpr SDL_Color kButtonPressed{255, 115, 84, 255};
constexpr SDL_Color kPanel{31, 38, 49, 255};
constexpr SDL_Color kCream{255, 241, 207, 255};
constexpr SDL_Color kMint{113, 214, 177, 255};
constexpr SDL_Color kMuted{151, 172, 184, 255};
constexpr SDL_Rect kBodyRect{34, 20, 570, 782};
constexpr SDL_Rect kScreenBezel{70, 55, 498, 378};
constexpr SDL_Rect kScreenRect{79, 64, 480, 360};

enum class Shape { Rectangle, Circle, Pill };

struct Button {
  SDL_Rect bounds;
  Action action;
  const char* label;
  Shape shape;
};

constexpr std::array<Button, 15> kButtons{{
    {{76, 30, 86, 28}, Action::PreviousSpeaker, "L1", Shape::Pill},
    {{174, 30, 86, 28}, Action::Queue, "L2", Shape::Pill},
    {{378, 30, 86, 28}, Action::Favorites, "R2", Shape::Pill},
    {{476, 30, 86, 28}, Action::NextSpeaker, "R1", Shape::Pill},
    {{151, 494, 42, 66}, Action::Up, "", Shape::Rectangle},
    {{151, 574, 42, 66}, Action::Down, "", Shape::Rectangle},
    {{85, 560, 66, 42}, Action::Left, "", Shape::Rectangle},
    {{193, 560, 66, 42}, Action::Right, "", Shape::Rectangle},
    {{425, 505, 48, 48}, Action::Context, "X", Shape::Circle},
    {{384, 550, 48, 48}, Action::Rooms, "Y", Shape::Circle},
    {{466, 550, 48, 48}, Action::Confirm, "A", Shape::Circle},
    {{425, 595, 48, 48}, Action::Back, "B", Shape::Circle},
    {{211, 688, 88, 28}, Action::Refresh, "SELECT", Shape::Pill},
    {{331, 688, 88, 28}, Action::Menu, "START", Shape::Pill},
    {{286, 744, 62, 30}, Action::ExitButton, "MENU", Shape::Pill},
}};

Action g_pressed_action = Action::None;
uint32_t g_pressed_until = 0;

void fill(SDL_Renderer* renderer, const SDL_Rect& rectangle, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer, &rectangle);
}

void outline(SDL_Renderer* renderer, const SDL_Rect& rectangle,
             SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawRect(renderer, &rectangle);
}

void filled_circle(SDL_Renderer* renderer, int center_x, int center_y,
                   int radius, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  for (int y = -radius; y <= radius; ++y) {
    const int width = static_cast<int>(
        std::sqrt(static_cast<double>(radius * radius - y * y)));
    SDL_RenderDrawLine(renderer, center_x - width, center_y + y,
                       center_x + width, center_y + y);
  }
}

bool contains(const SDL_Rect& rectangle, int x, int y) {
  return x >= rectangle.x && x < rectangle.x + rectangle.w &&
         y >= rectangle.y && y < rectangle.y + rectangle.h;
}

void draw_button(SDL_Renderer* renderer, const BitmapFont& font,
                 const Button& button, Action pressed) {
  const bool active = button.action == pressed;
  const SDL_Color color = active ? kButtonPressed : kButton;
  if (button.shape == Shape::Circle) {
    filled_circle(renderer, button.bounds.x + button.bounds.w / 2,
                  button.bounds.y + button.bounds.h / 2,
                  button.bounds.w / 2, color);
  } else {
    fill(renderer, button.bounds, color);
  }
  if (button.label[0] != '\0') {
    const int scale = button.bounds.w > 70 ? 1 : 2;
    const int x = button.bounds.x +
                  (button.bounds.w - font.width(button.label, scale)) / 2;
    const int y = button.bounds.y +
                  (button.bounds.h - 7 * scale) / 2;
    font.draw(button.label, x, y, scale, active ? kBezel : kCream);
  }
}

void draw_dpad_center(SDL_Renderer* renderer) {
  fill(renderer, SDL_Rect{151, 560, 42, 42}, kButton);
  filled_circle(renderer, 172, 581, 9, SDL_Color{35, 38, 44, 255});
}

void draw_speaker(SDL_Renderer* renderer) {
  SDL_SetRenderDrawColor(renderer, 103, 99, 94, 255);
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 18; ++column) {
      SDL_Rect dot{179 + column * 16 + (row % 2) * 6,
                   455 + row * 10, 4, 4};
      SDL_RenderFillRect(renderer, &dot);
    }
  }
}

void draw_onion_home(SDL_Renderer* renderer, const BitmapFont& font) {
  fill(renderer, kScreenRect, SDL_Color{8, 27, 46, 255});
  fill(renderer, SDL_Rect{kScreenRect.x, kScreenRect.y,
                          kScreenRect.w, 42}, SDL_Color{22, 58, 82, 255});
  font.draw("ONIONOS  APPS", kScreenRect.x + 18, kScreenRect.y + 14, 2,
            kCream);
  const SDL_Rect tile{kScreenRect.x + 34, kScreenRect.y + 86, 132, 132};
  fill(renderer, tile, SDL_Color{255, 241, 207, 255});
  fill(renderer, SDL_Rect{tile.x + 29, tile.y + 20, 74, 86},
       SDL_Color{7, 29, 59, 255});
  fill(renderer, SDL_Rect{tile.x + 40, tile.y + 31, 52, 36}, kMint);
  filled_circle(renderer, tile.x + 86, tile.y + 82, 8, kButtonPressed);
  font.draw("MIYONOS", tile.x + 24, tile.y + 146, 1, kCream);
  font.draw("MIYONOS EXITED CLEANLY", kScreenRect.x + 202,
            kScreenRect.y + 116, 1, kMint);
  font.draw("RETURNED TO THE APPS MENU", kScreenRect.x + 202,
            kScreenRect.y + 150, 1, kMuted);
}

}  // namespace

Action simulator_hit_test(int x, int y) {
  for (const Button& button : kButtons) {
    if (!contains(button.bounds, x, y)) continue;
    if (button.shape != Shape::Circle) return button.action;
    const int dx = x - (button.bounds.x + button.bounds.w / 2);
    const int dy = y - (button.bounds.y + button.bounds.h / 2);
    const int radius = button.bounds.w / 2;
    if (dx * dx + dy * dy <= radius * radius) return button.action;
  }
  return Action::None;
}

void simulator_note_press(Action action, bool pressed) {
  if (pressed) {
    g_pressed_action = action;
    g_pressed_until = SDL_GetTicks() + 140;
  } else if (g_pressed_action == action) {
    g_pressed_until = SDL_GetTicks() + 70;
  }
}

SimulatorShell::SimulatorShell(std::string scenario, bool live_sonos)
    : scenario_(std::move(scenario)), live_sonos_(live_sonos) {}

void SimulatorShell::draw(SDL_Renderer* renderer, SDL_Texture* app_frame,
                          bool onion_home) const {
  SDL_SetRenderDrawColor(renderer, kDesk.r, kDesk.g, kDesk.b, 255);
  SDL_RenderClear(renderer);
  fill(renderer, SDL_Rect{kBodyRect.x + 8, kBodyRect.y + 10,
                          kBodyRect.w, kBodyRect.h}, kBodyShadow);
  fill(renderer, kBodyRect, kBody);
  fill(renderer, kScreenBezel, kBezel);

  if (onion_home) {
    BitmapFont screen_font(renderer);
    draw_onion_home(renderer, screen_font);
  } else if (app_frame) {
    SDL_RenderCopy(renderer, app_frame, nullptr, &kScreenRect);
  }

  draw_speaker(renderer);
  BitmapFont font(renderer);
  const Action pressed = SDL_GetTicks() <= g_pressed_until
                             ? g_pressed_action
                             : Action::None;
  for (const Button& button : kButtons) {
    draw_button(renderer, font, button, pressed);
  }
  draw_dpad_center(renderer);
  font.draw_centered("MIYOO MINI PLUS", 655, 1,
                     SDL_Color{72, 70, 68, 255}, 319);

  const SDL_Rect panel{632, 20, 372, 782};
  fill(renderer, panel, kPanel);
  outline(renderer, panel, SDL_Color{62, 75, 94, 255});
  font.draw("MIYONOS SIMULATOR", 660, 52, 3, kCream);
  font.draw("EXACT 640 X 480 APP FRAME", 660, 91, 1, kMint);
  font.draw("MODE", 660, 145, 1, kMuted);
  font.draw(live_sonos_ ? "LIVE SONOS" : "SAFE LOCAL FIXTURE", 660, 170, 2,
            live_sonos_ ? kButtonPressed : kMint);
  font.draw("SCENARIO", 660, 224, 1, kMuted);
  font.draw(scenario_, 660, 249, 2, kCream, 310);
  font.draw("CLICK THE HANDHELD BUTTONS", 660, 318, 1, kCream);
  font.draw("OR USE THE KEYBOARD", 660, 343, 1, kCream);
  const std::array<const char*, 9> help{{
      "ARROWS   D-PAD", "Z / SPACE   A", "X   B", "A   X", "S   Y",
      "Q / W   L1 / R1", "1 / 2   L2 / R2", "ENTER   START",
      "R   SELECT   ESC   MENU"}};
  for (std::size_t index = 0; index < help.size(); ++index) {
    font.draw(help[index], 660, 395 + static_cast<int>(index) * 31, 1,
              index % 2 == 0 ? kMuted : kCream);
  }
  font.draw("SIMULATOR DATA IS ISOLATED", 660, 728, 1, kMint);
  font.draw("FROM THE PHYSICAL SD CARD", 660, 752, 1, kMint);
  SDL_RenderPresent(renderer);
}

}  // namespace miyonos
