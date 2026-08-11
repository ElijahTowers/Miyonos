#pragma once

#include <SDL.h>

#include <vector>

#include "platform/action.h"
#include "platform/button_mapping.h"
#include "platform/runtime_mode.h"

namespace miyonos {

class Input {
 public:
  explicit Input(RuntimeMode mode = RuntimeMode::Desktop);
  ~Input();

  Action translate(const SDL_Event& event);
  Action repeat_action(uint32_t now);
  void set_button_mapping(const ButtonMapping& mapping) { mapping_ = mapping; }
  void set_diagnostics(bool enabled) { diagnostics_ = enabled; }
  int last_keycode() const { return last_keycode_; }

 private:
  PhysicalButton from_key(SDL_Keycode key) const;
  PhysicalButton from_controller_button(Uint8 button) const;
  Action mapped_action(PhysicalButton button) const;
  void note_press(Action action, bool held, Action visual_action);
  void release(Action action, Action visual_action);
  void note_physical_button(PhysicalButton button, bool down);
  bool repeatable(Action action) const;
  bool delays_r2_favorites(PhysicalButton button, Action action) const;
  void begin_r2_favorites_press(Action action);
  Action end_r2_favorites_press();

  RuntimeMode mode_ = RuntimeMode::Desktop;
  bool diagnostics_ = false;
  int last_keycode_ = 0;
  uint32_t last_repeat_ms_ = 0;
  uint32_t held_since_ms_ = 0;
  Action held_action_ = Action::None;
  bool left_trigger_down_ = false;
  bool right_trigger_down_ = false;
  bool r2_favorites_pending_ = false;
  bool r2_shuffle_emitted_ = false;
  uint32_t r2_favorites_pressed_ms_ = 0;
  Action r2_favorites_short_action_ = Action::None;
  Action mouse_action_ = Action::None;
  PhysicalButton mouse_button_ = PhysicalButton::Count;
  ButtonMapping mapping_ = kDefaultButtonMapping;
  bool physical_menu_down_ = false;
  bool physical_start_down_ = false;
  bool recovery_chord_active_ = false;
  bool recovery_chord_emitted_ = false;
  uint32_t recovery_chord_started_ms_ = 0;
  std::vector<SDL_GameController*> controllers_;
};

}  // namespace miyonos
