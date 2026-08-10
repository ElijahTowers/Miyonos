#pragma once

#include <array>

namespace miyonos::strings {

inline constexpr const char* kAppName = "Miyonos";
inline constexpr const char* kTechnicalPreview = "Technical Preview";
inline constexpr const char* kNothingPlaying = "Nothing is playing";
inline constexpr const char* kUnsupported =
    "This item type is not supported yet.";
inline constexpr const char* kDisclaimer =
    "Miyonos is an independent community project and is not affiliated with "
    "or endorsed by Sonos, Inc. or the OnionOS project.";

inline constexpr std::array<const char*, 8> kMainMenu = {
    "Rooms & Groups", "Speaker Volumes", "Queue", "Favorites",
    "Settings",       "Help",            "About", "Diagnostics"};

inline constexpr std::array<const char*, 18> kSettings = {
    "Startup room or group", "Sonos volume step", "Seek interval",
    "Artwork cache size", "Automatic artwork", "External cover art over HTTPS",
    "Official Sonos product photos", "Polling intensity", "Screen dim timeout",
    "Prevent sleep", "Manual player IP", "Button hints", "Confirm before exit",
    "Button mapping", "Clear artwork cache", "Forget discovered system",
    "Reset all settings", "Diagnostics mode"};

inline constexpr std::array<const char*, 3> kOfflineActions = {
    "Search Again", "Enter Player IP", "Open Help"};

inline constexpr std::array<const char*, 3> kDiagnosticActions = {
    "Refresh Diagnostics", "Clear Logs", "Export Diagnostic Report"};

}  // namespace miyonos::strings
