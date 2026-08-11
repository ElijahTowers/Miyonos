#include "platform/input.h"

#include <algorithm>

#ifdef MIYONOS_ENABLE_SIMULATOR
#include "simulator/simulator_shell.h"
#endif

namespace miyonos {

namespace {

constexpr uint32_t kR2ShuffleHoldMs = 800;

}  // namespace

Input::Input(RuntimeMode mode) : mode_(mode) {
  const int joystick_count = SDL_NumJoysticks();
  for (int index = 0; index < joystick_count; ++index) {
    if (!SDL_IsGameController(index)) continue;
    SDL_GameController* controller = SDL_GameControllerOpen(index);
    if (controller) controllers_.push_back(controller);
  }
}

Input::~Input() {
  for (SDL_GameController* controller : controllers_) {
    if (controller) SDL_GameControllerClose(controller);
  }
}

bool Input::repeatable(Action action) const {
  return action == Action::Up || action == Action::Down ||
         action == Action::Left || action == Action::Right;
}

bool Input::delays_r2_favorites(PhysicalButton button, Action action) const {
  // R2's normal action remains Favorites. Delaying only that mapping lets a
  // held R2 become a clean Now Playing shuffle gesture without taking a
  // custom R2 assignment away from the owner.
  return button == PhysicalButton::R2 && action == Action::Favorites;
}

void Input::begin_r2_favorites_press(Action action) {
  r2_favorites_pending_ = true;
  r2_shuffle_emitted_ = false;
  r2_favorites_pressed_ms_ = SDL_GetTicks();
  r2_favorites_short_action_ = action;
}

Action Input::end_r2_favorites_press() {
  if (!r2_favorites_pending_) return Action::None;
  const Action short_action =
      r2_shuffle_emitted_ ? Action::None : r2_favorites_short_action_;
  r2_favorites_pending_ = false;
  r2_shuffle_emitted_ = false;
  r2_favorites_short_action_ = Action::None;
  return short_action;
}

Action Input::mapped_action(PhysicalButton button) const {
  const auto index = button_index(button);
  return index < mapping_.size() ? mapping_[index] : Action::None;
}

void Input::note_press(Action action, bool held, Action visual_action) {
#ifndef MIYONOS_ENABLE_SIMULATOR
  (void)visual_action;
#endif
  if (action == Action::None) return;
  if (held && repeatable(action)) {
    held_action_ = action;
    held_since_ms_ = SDL_GetTicks();
    last_repeat_ms_ = held_since_ms_;
  }
#ifdef MIYONOS_ENABLE_SIMULATOR
  if (mode_ == RuntimeMode::Simulator)
    simulator_note_press(visual_action, true);
#endif
}

void Input::release(Action action, Action visual_action) {
#ifndef MIYONOS_ENABLE_SIMULATOR
  (void)visual_action;
#endif
  if (action == held_action_) held_action_ = Action::None;
#ifdef MIYONOS_ENABLE_SIMULATOR
  if (mode_ == RuntimeMode::Simulator)
    simulator_note_press(visual_action, false);
#endif
}

void Input::note_physical_button(PhysicalButton button, bool down) {
  if (button == PhysicalButton::Menu) physical_menu_down_ = down;
  if (button == PhysicalButton::Start) physical_start_down_ = down;
  if (physical_menu_down_ && physical_start_down_) {
    if (!recovery_chord_active_) {
      recovery_chord_active_ = true;
      recovery_chord_emitted_ = false;
      recovery_chord_started_ms_ = SDL_GetTicks();
    }
  } else {
    recovery_chord_active_ = false;
    recovery_chord_emitted_ = false;
  }
}

PhysicalButton Input::from_key(SDL_Keycode key) const {
  switch (key) {
    case SDLK_UP: return PhysicalButton::Up;
    case SDLK_DOWN: return PhysicalButton::Down;
    case SDLK_LEFT: return PhysicalButton::Left;
    case SDLK_RIGHT: return PhysicalButton::Right;
    // Verified OnionOS software mapping: A=Space, B=Left Ctrl,
    // X=Left Shift, Y=Left Alt, L1=E, R1=T, L2=Tab, R2=Backspace.
    case SDLK_SPACE:
    case SDLK_z: return PhysicalButton::A;
    case SDLK_LCTRL:
    case SDLK_x: return PhysicalButton::B;
    case SDLK_LSHIFT:
    case SDLK_a: return PhysicalButton::X;
    case SDLK_LALT:
    case SDLK_s: return PhysicalButton::Y;
    case SDLK_e:
    case SDLK_q: return PhysicalButton::L1;
    case SDLK_t:
    case SDLK_w: return PhysicalButton::R1;
    case SDLK_TAB:
    case SDLK_1: return PhysicalButton::L2;
    case SDLK_BACKSPACE:
    case SDLK_2: return PhysicalButton::R2;
    case SDLK_RETURN: return PhysicalButton::Start;
    case SDLK_RCTRL:
    case SDLK_r: return PhysicalButton::Select;
    case SDLK_ESCAPE: return PhysicalButton::Menu;
    default: return PhysicalButton::Count;
  }
}

PhysicalButton Input::from_controller_button(Uint8 button) const {
  switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return PhysicalButton::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return PhysicalButton::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return PhysicalButton::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return PhysicalButton::Right;
    case SDL_CONTROLLER_BUTTON_A: return PhysicalButton::A;
    case SDL_CONTROLLER_BUTTON_B: return PhysicalButton::B;
    case SDL_CONTROLLER_BUTTON_X: return PhysicalButton::X;
    case SDL_CONTROLLER_BUTTON_Y: return PhysicalButton::Y;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return PhysicalButton::L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return PhysicalButton::R1;
    case SDL_CONTROLLER_BUTTON_START: return PhysicalButton::Start;
    case SDL_CONTROLLER_BUTTON_BACK: return PhysicalButton::Select;
    case SDL_CONTROLLER_BUTTON_GUIDE: return PhysicalButton::Menu;
    default: return PhysicalButton::Count;
  }
}

Action Input::translate(const SDL_Event& event) {
  if (event.type == SDL_KEYDOWN) {
    const SDL_Keycode key = event.key.keysym.sym;
    const PhysicalButton button = from_key(key);
    const Action action = mapped_action(button);
    const Action visual_action = default_button_action(button);
    if (diagnostics_) last_keycode_ = static_cast<int>(key);
    if (event.key.repeat != 0) {
      const uint32_t now = SDL_GetTicks();
      if (!repeatable(action) || now - last_repeat_ms_ < 160) {
        return Action::None;
      }
      last_repeat_ms_ = now;
    } else {
      last_repeat_ms_ = SDL_GetTicks();
      note_physical_button(button, true);
      // SDL already supplies rate-limited keyboard repeat events. The custom
      // held-button timer is reserved for mouse and controller input.
      note_press(action, false, visual_action);
      if (delays_r2_favorites(button, action)) {
        begin_r2_favorites_press(action);
        return Action::None;
      }
    }
    return action;
  }
  if (event.type == SDL_KEYUP) {
    const PhysicalButton button = from_key(event.key.keysym.sym);
    note_physical_button(button, false);
    release(mapped_action(button), default_button_action(button));
    return button == PhysicalButton::R2 ? end_r2_favorites_press()
                                        : Action::None;
  }
  if (event.type == SDL_CONTROLLERDEVICEADDED) {
    SDL_GameController* controller = SDL_GameControllerOpen(event.cdevice.which);
    if (controller) controllers_.push_back(controller);
    return Action::None;
  }
  if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
    const SDL_JoystickID instance = event.cdevice.which;
    auto found = std::find_if(
        controllers_.begin(), controllers_.end(),
        [instance](SDL_GameController* controller) {
          return controller &&
                 SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) ==
                     instance;
        });
    if (found != controllers_.end()) {
      SDL_GameControllerClose(*found);
      controllers_.erase(found);
    }
    return Action::None;
  }
  if (event.type == SDL_CONTROLLERBUTTONDOWN) {
    const PhysicalButton button = from_controller_button(event.cbutton.button);
    const Action action = mapped_action(button);
    last_keycode_ = 1000 + event.cbutton.button;
    note_physical_button(button, true);
    note_press(action, true, default_button_action(button));
    return action;
  }
  if (event.type == SDL_CONTROLLERBUTTONUP) {
    const PhysicalButton button = from_controller_button(event.cbutton.button);
    note_physical_button(button, false);
    release(mapped_action(button), default_button_action(button));
    return Action::None;
  }
  if (event.type == SDL_CONTROLLERAXISMOTION) {
    constexpr Sint16 kTriggerThreshold = 16000;
    if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
      const bool down = event.caxis.value >= kTriggerThreshold;
      if (down && !left_trigger_down_) {
        left_trigger_down_ = true;
        const Action action = mapped_action(PhysicalButton::L2);
        note_physical_button(PhysicalButton::L2, true);
        note_press(action, false, Action::Queue);
        return action;
      }
      if (!down) {
        left_trigger_down_ = false;
        note_physical_button(PhysicalButton::L2, false);
        release(mapped_action(PhysicalButton::L2), Action::Queue);
      }
    } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
      const bool down = event.caxis.value >= kTriggerThreshold;
      if (down && !right_trigger_down_) {
        right_trigger_down_ = true;
        const Action action = mapped_action(PhysicalButton::R2);
        note_physical_button(PhysicalButton::R2, true);
        note_press(action, false, Action::Favorites);
        if (delays_r2_favorites(PhysicalButton::R2, action)) {
          begin_r2_favorites_press(action);
          return Action::None;
        }
        return action;
      }
      if (!down && right_trigger_down_) {
        right_trigger_down_ = false;
        note_physical_button(PhysicalButton::R2, false);
        release(mapped_action(PhysicalButton::R2), Action::Favorites);
        return end_r2_favorites_press();
      }
    }
    return Action::None;
  }
#ifdef MIYONOS_ENABLE_SIMULATOR
  if (mode_ == RuntimeMode::Simulator &&
      event.type == SDL_MOUSEBUTTONDOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    int logical_x = event.button.x;
    int logical_y = event.button.y;
    SDL_Window* window = SDL_GetWindowFromID(event.button.windowID);
    int window_width = 0;
    int window_height = 0;
    if (window) SDL_GetWindowSize(window, &window_width, &window_height);
    if (window_width > 0 && window_height > 0) {
      logical_x = logical_x * kSimulatorWindowWidth / window_width;
      logical_y = logical_y * kSimulatorWindowHeight / window_height;
    }
    const Action visual_action = simulator_hit_test(logical_x, logical_y);
    mouse_button_ = physical_button_for_default_action(visual_action);
    mouse_action_ = mapped_action(mouse_button_);
    note_physical_button(mouse_button_, true);
    note_press(mouse_action_, true, visual_action);
    if (delays_r2_favorites(mouse_button_, mouse_action_)) {
      begin_r2_favorites_press(mouse_action_);
      return Action::None;
    }
    return mouse_action_;
  }
  if (mode_ == RuntimeMode::Simulator &&
      event.type == SDL_MOUSEBUTTONUP &&
      event.button.button == SDL_BUTTON_LEFT) {
    note_physical_button(mouse_button_, false);
    release(mouse_action_, default_button_action(mouse_button_));
    const Action released = mouse_button_ == PhysicalButton::R2
                                ? end_r2_favorites_press()
                                : Action::None;
    mouse_action_ = Action::None;
    mouse_button_ = PhysicalButton::Count;
    return released;
  }
#endif
  return Action::None;
}

Action Input::repeat_action(uint32_t now) {
  if (recovery_chord_active_ && !recovery_chord_emitted_ &&
      now - recovery_chord_started_ms_ >= 3000) {
    recovery_chord_emitted_ = true;
    held_action_ = Action::None;
    return Action::ResetButtonMapping;
  }
  if (r2_favorites_pending_ && !r2_shuffle_emitted_ &&
      now - r2_favorites_pressed_ms_ >= kR2ShuffleHoldMs) {
    r2_shuffle_emitted_ = true;
    return Action::ToggleShuffle;
  }
  if (!repeatable(held_action_)) return Action::None;
  if (now - held_since_ms_ < 420 || now - last_repeat_ms_ < 160) {
    return Action::None;
  }
  last_repeat_ms_ = now;
  return held_action_;
}

}  // namespace miyonos
