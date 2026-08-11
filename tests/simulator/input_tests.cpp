#include <SDL.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <utility>

#include "platform/action.h"
#include "platform/input.h"
#include "simulator/simulator_shell.h"

using namespace miyonos;

namespace {

int checks = 0;
int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      ++failures;                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << " check failed: "           \
                << #condition << '\n';                                         \
    }                                                                          \
  } while (false)

SDL_Event key_event(Uint32 type, SDL_Keycode key, Uint8 repeat = 0) {
  SDL_Event event{};
  event.type = type;
  event.key.type = type;
  event.key.keysym.sym = key;
  event.key.repeat = repeat;
  return event;
}

SDL_Event mouse_event(Uint32 type, int x, int y) {
  SDL_Event event{};
  event.type = type;
  event.button.type = type;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = x;
  event.button.y = y;
  return event;
}

SDL_Event controller_button_event(Uint32 type, Uint8 button) {
  SDL_Event event{};
  event.type = type;
  event.cbutton.type = type;
  event.cbutton.button = button;
  return event;
}

void test_keyboard() {
  Input input(RuntimeMode::Simulator);
  const std::array<std::pair<SDL_Keycode, Action>, 20> mappings{{
      {SDLK_UP, Action::Up},          {SDLK_DOWN, Action::Down},
      {SDLK_LEFT, Action::Left},      {SDLK_RIGHT, Action::Right},
      {SDLK_SPACE, Action::Confirm},  {SDLK_z, Action::Confirm},
      {SDLK_LCTRL, Action::Back},     {SDLK_x, Action::Back},
      {SDLK_LSHIFT, Action::Context}, {SDLK_a, Action::Context},
      {SDLK_LALT, Action::Rooms},     {SDLK_s, Action::Rooms},
      {SDLK_q, Action::SpeakerVolumes}, {SDLK_w, Action::NextGroup},
      {SDLK_1, Action::Queue},        {SDLK_2, Action::Favorites},
      {SDLK_RETURN, Action::Menu},    {SDLK_r, Action::Controls},
      {SDLK_ESCAPE, Action::ExitButton}, {SDLK_F12, Action::None},
  }};
  for (const auto& mapping : mappings) {
    const bool delayed_r2 = mapping.first == SDLK_2;
    CHECK(input.translate(key_event(SDL_KEYDOWN, mapping.first)) ==
          (delayed_r2 ? Action::None : mapping.second));
    CHECK(input.translate(key_event(SDL_KEYUP, mapping.first)) ==
          (delayed_r2 ? mapping.second : Action::None));
  }

  CHECK(input.translate(key_event(SDL_KEYDOWN, SDLK_BACKSPACE)) == Action::None);
  const uint32_t r2_hold_started = SDL_GetTicks();
  CHECK(input.repeat_action(r2_hold_started + 900) == Action::ToggleShuffle);
  CHECK(input.repeat_action(r2_hold_started + 1000) == Action::None);
  CHECK(input.translate(key_event(SDL_KEYUP, SDLK_BACKSPACE)) == Action::None);
}

void test_mouse_and_hit_areas() {
  Input input(RuntimeMode::Simulator);
  struct Hit {
    int x;
    int y;
    Action action;
  };
  const std::array<Hit, 15> hits{{
      {119, 44, Action::SpeakerVolumes}, {217, 44, Action::Queue},
      {421, 44, Action::Favorites},  {519, 44, Action::NextGroup},
      {172, 527, Action::Up},        {172, 607, Action::Down},
      {118, 581, Action::Left},      {226, 581, Action::Right},
      {449, 529, Action::Context},   {408, 574, Action::Rooms},
      {490, 574, Action::Confirm},   {449, 619, Action::Back},
      {255, 702, Action::Controls},  {375, 702, Action::Menu},
      {317, 759, Action::ExitButton},
  }};
  for (const Hit& hit : hits) {
    CHECK(simulator_hit_test(hit.x, hit.y) == hit.action);
    CHECK(input.translate(mouse_event(SDL_MOUSEBUTTONDOWN, hit.x, hit.y)) ==
          (hit.action == Action::Favorites ? Action::None : hit.action));
    CHECK(input.translate(mouse_event(SDL_MOUSEBUTTONUP, hit.x, hit.y)) ==
          (hit.action == Action::Favorites ? hit.action : Action::None));
  }
  CHECK(simulator_hit_test(0, 0) == Action::None);
  CHECK(simulator_hit_test(384, 550) == Action::None);

  CHECK(input.translate(mouse_event(SDL_MOUSEBUTTONDOWN, 172, 527)) ==
        Action::Up);
  CHECK(input.repeat_action(SDL_GetTicks() + 500) == Action::Up);
  input.translate(mouse_event(SDL_MOUSEBUTTONUP, 900, 700));
  CHECK(input.repeat_action(SDL_GetTicks() + 1000) == Action::None);

  CHECK(input.translate(mouse_event(SDL_MOUSEBUTTONDOWN, 490, 574)) ==
        Action::Confirm);
  CHECK(input.repeat_action(SDL_GetTicks() + 1000) == Action::None);
  input.translate(mouse_event(SDL_MOUSEBUTTONUP, 490, 574));
}

void test_gamepad() {
  Input input(RuntimeMode::Simulator);
  const std::array<std::pair<Uint8, Action>, 13> mappings{{
      {SDL_CONTROLLER_BUTTON_DPAD_UP, Action::Up},
      {SDL_CONTROLLER_BUTTON_DPAD_DOWN, Action::Down},
      {SDL_CONTROLLER_BUTTON_DPAD_LEFT, Action::Left},
      {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Action::Right},
      {SDL_CONTROLLER_BUTTON_A, Action::Confirm},
      {SDL_CONTROLLER_BUTTON_B, Action::Back},
      {SDL_CONTROLLER_BUTTON_X, Action::Context},
      {SDL_CONTROLLER_BUTTON_Y, Action::Rooms},
      {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Action::SpeakerVolumes},
      {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Action::NextGroup},
      {SDL_CONTROLLER_BUTTON_START, Action::Menu},
      {SDL_CONTROLLER_BUTTON_BACK, Action::Controls},
      {SDL_CONTROLLER_BUTTON_GUIDE, Action::ExitButton},
  }};
  for (const auto& mapping : mappings) {
    CHECK(input.translate(controller_button_event(
              SDL_CONTROLLERBUTTONDOWN, mapping.first)) == mapping.second);
    CHECK(input.translate(controller_button_event(
              SDL_CONTROLLERBUTTONUP, mapping.first)) == Action::None);
  }

  SDL_Event trigger{};
  trigger.type = SDL_CONTROLLERAXISMOTION;
  trigger.caxis.type = SDL_CONTROLLERAXISMOTION;
  trigger.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
  trigger.caxis.value = 20000;
  CHECK(input.translate(trigger) == Action::Queue);
  CHECK(input.translate(trigger) == Action::None);
  trigger.caxis.value = 0;
  CHECK(input.translate(trigger) == Action::None);

  trigger.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
  trigger.caxis.value = 20000;
  CHECK(input.translate(trigger) == Action::None);
  const uint32_t r2_trigger_hold_started = SDL_GetTicks();
  CHECK(input.repeat_action(r2_trigger_hold_started + 900) ==
        Action::ToggleShuffle);
  trigger.caxis.value = 0;
  CHECK(input.translate(trigger) == Action::None);
}

void test_wake_only_press_suppression() {
  Input input(RuntimeMode::Simulator);

  // AppRuntime ignores the initial action, then tells Input to discard every
  // result from that physical press until release. This keeps the same A
  // press from pausing music after it wakes Battery Saver.
  CHECK(input.translate(key_event(SDL_KEYDOWN, SDLK_SPACE)) == Action::Confirm);
  input.suppress_current_press();
  CHECK(input.translate(key_event(SDL_KEYUP, SDLK_SPACE)) == Action::None);

  // R2 has a delayed short action and a hold action, both of which must stay
  // quiet when it was the wake-only press.
  CHECK(input.translate(key_event(SDL_KEYDOWN, SDLK_BACKSPACE)) == Action::None);
  input.suppress_current_press();
  const uint32_t r2_started = SDL_GetTicks();
  CHECK(input.repeat_action(r2_started + 1000) == Action::None);
  CHECK(input.translate(key_event(SDL_KEYUP, SDLK_BACKSPACE)) == Action::None);

  // A held D-pad button cannot start repeating volume or navigation after it
  // has been used solely to wake the screen.
  CHECK(input.translate(controller_button_event(
            SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_DPAD_UP)) ==
        Action::Up);
  input.suppress_current_press();
  CHECK(input.repeat_action(SDL_GetTicks() + 1000) == Action::None);
  CHECK(input.translate(controller_button_event(
            SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_DPAD_UP)) ==
        Action::None);
  CHECK(input.repeat_action(SDL_GetTicks() + 1200) == Action::None);
}

void test_custom_mapping_and_recovery_chord() {
  Input input(RuntimeMode::Simulator);
  ButtonMapping mapping = kDefaultButtonMapping;
  mapping[button_index(PhysicalButton::Left)] = Action::Next;
  mapping[button_index(PhysicalButton::R1)] = Action::SeekForward;
  input.set_button_mapping(mapping);
  CHECK(input.translate(key_event(SDL_KEYDOWN, SDLK_LEFT)) == Action::Next);
  CHECK(input.translate(key_event(SDL_KEYUP, SDLK_LEFT)) == Action::None);
  CHECK(input.translate(key_event(SDL_KEYDOWN, SDLK_w)) ==
        Action::SeekForward);
  CHECK(input.translate(key_event(SDL_KEYUP, SDLK_w)) == Action::None);
  mapping[button_index(PhysicalButton::R2)] = Action::Refresh;
  input.set_button_mapping(mapping);
  CHECK(input.translate(key_event(SDL_KEYDOWN, SDLK_2)) == Action::Refresh);
  CHECK(input.translate(key_event(SDL_KEYUP, SDLK_2)) == Action::None);

  input.translate(key_event(SDL_KEYDOWN, SDLK_ESCAPE));
  input.translate(key_event(SDL_KEYDOWN, SDLK_RETURN));
  const uint32_t now = SDL_GetTicks();
  CHECK(input.repeat_action(now + 2500) != Action::ResetButtonMapping);
  CHECK(input.repeat_action(now + 3500) == Action::ResetButtonMapping);
  CHECK(input.repeat_action(now + 4000) != Action::ResetButtonMapping);
  input.translate(key_event(SDL_KEYUP, SDLK_RETURN));
  input.translate(key_event(SDL_KEYUP, SDLK_ESCAPE));
}

}  // namespace

int main() {
  if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
    return 1;
  }
  test_keyboard();
  test_mouse_and_hit_areas();
  test_gamepad();
  test_wake_only_press_suppression();
  test_custom_mapping_and_recovery_chord();
  SDL_Quit();
  std::cout << checks << " simulator input checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
