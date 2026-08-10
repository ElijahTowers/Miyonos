#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

#include "platform/action.h"

namespace miyonos {

enum class PhysicalButton : std::size_t {
  Up,
  Down,
  Left,
  Right,
  A,
  B,
  X,
  Y,
  L1,
  R1,
  L2,
  R2,
  Start,
  Select,
  Menu,
  Count
};

inline constexpr std::size_t kPhysicalButtonCount =
    static_cast<std::size_t>(PhysicalButton::Count);
using ButtonMapping = std::array<Action, kPhysicalButtonCount>;

inline constexpr ButtonMapping kLegacyDefaultButtonMapping{{
    Action::Up,        Action::Down,      Action::Left,
    Action::Right,     Action::Confirm,   Action::Back,
    Action::Context,   Action::Rooms,     Action::Previous,
    Action::Next,      Action::Queue,     Action::Favorites,
    Action::Menu,      Action::Refresh,   Action::ExitButton,
}};

// Default layout before the Controls overlay was introduced. Keep this so an
// existing untouched installation receives the new, simpler Select behavior.
inline constexpr ButtonMapping kRefreshDefaultButtonMapping{{
    Action::Up,        Action::Down,      Action::Left,
    Action::Right,     Action::Confirm,   Action::Back,
    Action::Context,   Action::Rooms,     Action::PreviousSpeaker,
    Action::NextSpeaker, Action::Queue,   Action::Favorites,
    Action::Menu,      Action::Refresh,   Action::ExitButton,
}};

// Default layout before L1 became the dedicated Speaker Volumes shortcut.
// Keep this so an existing untouched installation receives the new shortcut.
inline constexpr ButtonMapping kSpeakerVolumesPreviousDefaultButtonMapping{{
    Action::Up,        Action::Down,      Action::Left,
    Action::Right,     Action::Confirm,   Action::Back,
    Action::Context,   Action::Rooms,     Action::PreviousSpeaker,
    Action::NextSpeaker, Action::Queue,   Action::Favorites,
    Action::Menu,      Action::Controls,  Action::ExitButton,
}};

// Default layout before R1 became the group switcher. Keep this so an
// untouched installation receives the clearer Now Playing behavior.
inline constexpr ButtonMapping kGroupSwitchPreviousDefaultButtonMapping{{
    Action::Up,        Action::Down,      Action::Left,
    Action::Right,     Action::Confirm,   Action::Back,
    Action::Context,   Action::Rooms,     Action::SpeakerVolumes,
    Action::NextSpeaker, Action::Queue,   Action::Favorites,
    Action::Menu,      Action::Controls,  Action::ExitButton,
}};

inline constexpr ButtonMapping kDefaultButtonMapping{{
    Action::Up,        Action::Down,      Action::Left,
    Action::Right,     Action::Confirm,   Action::Back,
    Action::Context,   Action::Rooms,     Action::SpeakerVolumes,
    Action::NextGroup, Action::Queue,     Action::Favorites,
    Action::Menu,      Action::Controls,  Action::ExitButton,
}};

inline constexpr std::array<Action, 23> kMappableActions{{
    Action::None,         Action::Up,           Action::Down,
    Action::Left,         Action::Right,        Action::Confirm,
    Action::Back,         Action::Context,      Action::Rooms,
    Action::SpeakerVolumes, Action::PreviousSpeaker, Action::NextSpeaker,
    Action::NextGroup,    Action::Previous,
    Action::Next,         Action::SeekBackward,
    Action::SeekForward,  Action::Queue,        Action::Favorites,
    Action::Menu,         Action::Controls,     Action::Refresh,
    Action::ExitButton,
}};

inline constexpr std::array<const char*, kPhysicalButtonCount>
    kPhysicalButtonNames{{"D-Pad Up", "D-Pad Down", "D-Pad Left",
                          "D-Pad Right", "A", "B", "X", "Y", "L1",
                          "R1", "L2", "R2", "START", "SELECT", "MENU"}};

inline constexpr std::array<const char*, kPhysicalButtonCount>
    kPhysicalButtonIds{{"up", "down", "left", "right", "a", "b", "x",
                        "y", "l1", "r1", "l2", "r2", "start", "select",
                        "menu"}};

inline constexpr std::size_t button_index(PhysicalButton button) {
  return static_cast<std::size_t>(button);
}

inline const char* physical_button_name(PhysicalButton button) {
  const auto index = button_index(button);
  return index < kPhysicalButtonCount ? kPhysicalButtonNames[index]
                                     : "Unknown";
}

inline const char* physical_button_id(PhysicalButton button) {
  const auto index = button_index(button);
  return index < kPhysicalButtonCount ? kPhysicalButtonIds[index] : "unknown";
}

inline Action default_button_action(PhysicalButton button) {
  const auto index = button_index(button);
  return index < kPhysicalButtonCount ? kDefaultButtonMapping[index]
                                     : Action::None;
}

inline PhysicalButton physical_button_for_default_action(Action action) {
  const auto found = std::find(kDefaultButtonMapping.begin(),
                               kDefaultButtonMapping.end(), action);
  return found == kDefaultButtonMapping.end()
             ? PhysicalButton::Count
             : static_cast<PhysicalButton>(
                   std::distance(kDefaultButtonMapping.begin(), found));
}

inline const char* action_id(Action action) {
  switch (action) {
    case Action::None: return "none";
    case Action::Up: return "up";
    case Action::Down: return "down";
    case Action::Left: return "left";
    case Action::Right: return "right";
    case Action::Confirm: return "confirm";
    case Action::Back: return "back";
    case Action::Context: return "mute_context";
    case Action::Rooms: return "rooms";
    case Action::SpeakerVolumes: return "speaker_volumes";
    case Action::PreviousSpeaker: return "previous_speaker";
    case Action::NextSpeaker: return "next_speaker";
    case Action::NextGroup: return "next_group";
    case Action::Previous: return "previous_track";
    case Action::Next: return "next_track";
    case Action::SeekBackward: return "seek_backward";
    case Action::SeekForward: return "seek_forward";
    case Action::Queue: return "queue";
    case Action::Favorites: return "favorites";
    case Action::Menu: return "main_menu";
    case Action::Controls: return "controls";
    case Action::Refresh: return "refresh";
    case Action::ExitButton: return "exit";
    case Action::ResetButtonMapping: return "reset_button_mapping";
  }
  return "none";
}

inline const char* action_name(Action action) {
  switch (action) {
    case Action::None: return "No action";
    case Action::Up: return "Up / Volume Up";
    case Action::Down: return "Down / Volume Down";
    case Action::Left: return "Left / Previous Track";
    case Action::Right: return "Right / Next Track";
    case Action::Confirm: return "Confirm / Play-Pause";
    case Action::Back: return "Back / Cancel";
    case Action::Context: return "Mute / Context";
    case Action::Rooms: return "Rooms & Groups";
    case Action::SpeakerVolumes: return "Speaker Volumes";
    case Action::PreviousSpeaker: return "Previous Speaker";
    case Action::NextSpeaker: return "Next Speaker";
    case Action::NextGroup: return "Next Group";
    case Action::Previous: return "Previous Track";
    case Action::Next: return "Next Track";
    case Action::SeekBackward: return "Seek Backward";
    case Action::SeekForward: return "Seek Forward";
    case Action::Queue: return "Queue";
    case Action::Favorites: return "Favorites";
    case Action::Menu: return "Main Menu";
    case Action::Controls: return "Show Controls";
    case Action::Refresh: return "Refresh";
    case Action::ExitButton: return "Exit Miyonos";
    case Action::ResetButtonMapping: return "Restore Button Mapping";
  }
  return "No action";
}

inline bool action_from_id(const std::string& id, Action* action) {
  if (!action) return false;
  for (const Action candidate : kMappableActions) {
    if (id == action_id(candidate)) {
      *action = candidate;
      return true;
    }
  }
  return false;
}

inline Action cycle_mappable_action(Action current, int direction) {
  const auto found =
      std::find(kMappableActions.begin(), kMappableActions.end(), current);
  int index = found == kMappableActions.end()
                  ? 0
                  : static_cast<int>(
                        std::distance(kMappableActions.begin(), found));
  const int count = static_cast<int>(kMappableActions.size());
  index = (index + direction % count + count) % count;
  return kMappableActions[static_cast<std::size_t>(index)];
}

inline bool button_mapping_is_safe(const ButtonMapping& mapping) {
  const auto contains = [&](Action action) {
    return std::find(mapping.begin(), mapping.end(), action) != mapping.end();
  };
  return contains(Action::Up) && contains(Action::Down) &&
         contains(Action::Confirm) && contains(Action::Back) &&
         contains(Action::ExitButton);
}

}  // namespace miyonos
