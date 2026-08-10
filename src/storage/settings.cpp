#include "storage/settings.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "network/http.h"

namespace fs = std::filesystem;

namespace miyonos {

namespace {

std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

bool parse_bool(const std::string& value, bool fallback) {
  const auto lower = lowercase(trim(value));
  if (lower == "1" || lower == "true" || lower == "on") return true;
  if (lower == "0" || lower == "false" || lower == "off") return false;
  return fallback;
}

int parse_int(const std::string& value, int fallback) {
  int parsed = 0;
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  return result.ec == std::errc{} ? parsed : fallback;
}

std::vector<std::string> split(const std::string& value) {
  std::vector<std::string> result;
  std::istringstream stream(value);
  std::string part;
  while (std::getline(stream, part, ',')) {
    part = trim(part);
    if (!part.empty()) result.push_back(part);
  }
  return result;
}

std::string join(const std::vector<std::string>& values) {
  std::ostringstream result;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) result << ',';
    result << values[i];
  }
  return result.str();
}

bool safe_value(const std::string& value) {
  return value.size() <= 4096 && value.find_first_of("\r\n") == std::string::npos;
}

std::map<std::string, std::string> read_fields(const std::string& path) {
  std::map<std::string, std::string> fields;
  std::ifstream input(path);
  std::string line;
  std::size_t total = 0;
  while (std::getline(input, line) && total < 128 * 1024) {
    total += line.size();
    const auto separator = line.find('=');
    if (separator == std::string::npos || line.empty() || line.front() == '#')
      continue;
    const std::string key = trim(line.substr(0, separator));
    const std::string value = trim(line.substr(separator + 1));
    if (!key.empty() && key.size() <= 128 && safe_value(value)) {
      fields[key] = value;
    }
  }
  return fields;
}

}  // namespace

void validate_settings(Settings& settings) {
  const int allowed_steps[] = {1, 2, 3, 5};
  if (std::find(std::begin(allowed_steps), std::end(allowed_steps),
                settings.volume_step) == std::end(allowed_steps)) {
    settings.volume_step = 2;
  }
  const int allowed_seek[] = {5, 10, 15, 30};
  if (std::find(std::begin(allowed_seek), std::end(allowed_seek),
                settings.seek_seconds) == std::end(allowed_seek)) {
    settings.seek_seconds = 10;
  }
  settings.artwork_cache_mb =
      std::max(5, std::min(100, settings.artwork_cache_mb));
  settings.dim_timeout_seconds =
      std::max(0, std::min(3600, settings.dim_timeout_seconds));
  auto clean_ips = [](std::vector<std::string>& ips) {
    std::vector<std::string> clean;
    for (const auto& ip : ips) {
      if (valid_ipv4(ip) &&
          std::find(clean.begin(), clean.end(), ip) == clean.end()) {
        clean.push_back(ip);
      }
      if (clean.size() == 16) break;
    }
    ips = std::move(clean);
  };
  clean_ips(settings.manual_ips);
  clean_ips(settings.cached_ips);
  if (!button_mapping_is_safe(settings.button_mapping)) {
    settings.button_mapping = kDefaultButtonMapping;
  }
}

SettingsStore::SettingsStore(std::string data_directory)
    : directory_(std::move(data_directory)) {}

std::string SettingsStore::path() const {
  return (fs::path(directory_) / "settings.ini").string();
}

Settings SettingsStore::load(std::string* warning) const {
  Settings settings;
  const auto fields = read_fields(path());
  if (fields.empty()) return settings;
  auto get = [&](const std::string& key, const std::string& fallback = {}) {
    const auto found = fields.find(key);
    return found == fields.end() ? fallback : found->second;
  };
  settings.schema_version = parse_int(get("schema_version", "1"), 1);
  if (settings.schema_version > 1 && warning) {
    *warning = "Settings were created by a newer Miyonos version; known fields "
               "were loaded and unknown fields were preserved.";
  }
  const auto startup = get("startup_mode", "last");
  settings.startup_mode = startup == "specific"
                              ? StartupMode::SpecificRoom
                              : startup == "ask" ? StartupMode::AskEveryTime
                                                  : StartupMode::LastUsed;
  settings.startup_room_uuid = get("startup_room_uuid");
  settings.volume_step = parse_int(get("volume_step", "2"), 2);
  settings.seek_seconds = parse_int(get("seek_seconds", "10"), 10);
  settings.artwork_cache_mb =
      parse_int(get("artwork_cache_mb", "20"), 20);
  settings.auto_artwork = parse_bool(get("auto_artwork", "1"), true);
  settings.spotify_https_artwork =
      parse_bool(get("spotify_https_artwork", "1"), true);
  settings.official_sonos_product_photos =
      parse_bool(get("official_sonos_product_photos", "1"), true);
  // Before this setting revision, artwork downloads were opt-in. Apply the
  // new recommended defaults once to existing installations, then preserve
  // any choice the owner makes afterwards.
  const int content_defaults_version =
      parse_int(get("content_defaults_version", "0"), 0);
  if (content_defaults_version < 1) {
    settings.auto_artwork = true;
    settings.spotify_https_artwork = true;
    settings.official_sonos_product_photos = true;
  }
  const auto polling = get("polling", "balanced");
  settings.polling = polling == "battery"
                         ? PollingIntensity::BatterySaver
                         : polling == "responsive"
                               ? PollingIntensity::Responsive
                               : PollingIntensity::Balanced;
  settings.dim_timeout_seconds =
      parse_int(get("dim_timeout_seconds", "120"), 120);
  settings.idle_battery_saver =
      parse_bool(get("idle_battery_saver", "1"), true);
  settings.prevent_sleep = parse_bool(get("prevent_sleep", "1"), true);
  settings.manual_ips = split(get("manual_ips"));
  const auto hints = get("button_hints", "briefly");
  settings.button_hints = hints == "always"
                              ? ButtonHints::Always
                              : hints == "never" ? ButtonHints::Never
                                                  : ButtonHints::Briefly;
  settings.confirm_exit = parse_bool(get("confirm_exit", "1"), true);
  settings.diagnostics_mode =
      parse_bool(get("diagnostics_mode", "0"), false);
  settings.last_group_id = get("last_group_id");
  settings.last_room_uuid = get("last_room_uuid");
  settings.cached_ips = split(get("cached_ips"));
  settings.playlist_context_group_id = get("playlist_context_group_id");
  settings.playlist_context_title = get("playlist_context_title");
  settings.playlist_context_object_id = get("playlist_context_object_id");
  settings.playlist_context_artwork_uri = get("playlist_context_artwork_uri");
  settings.playlist_context_queue_fingerprint =
      get("playlist_context_queue_fingerprint");
  for (std::size_t index = 0; index < kPhysicalButtonCount; ++index) {
    const auto button = static_cast<PhysicalButton>(index);
    const std::string value = get(std::string("button_") +
                                  physical_button_id(button));
    Action action = Action::None;
    if (!value.empty() && action_from_id(value, &action)) {
      settings.button_mapping[index] = action;
    }
  }
  if (settings.button_mapping == kLegacyDefaultButtonMapping ||
      settings.button_mapping == kRefreshDefaultButtonMapping ||
      settings.button_mapping == kSpeakerVolumesPreviousDefaultButtonMapping) {
    settings.button_mapping = kDefaultButtonMapping;
  }
  const std::vector<std::string> known = {
      "schema_version", "startup_mode", "startup_room_uuid", "volume_step",
      "seek_seconds", "artwork_cache_mb", "auto_artwork",
      "spotify_https_artwork", "official_sonos_product_photos",
      "content_defaults_version", "polling",
      "dim_timeout_seconds", "idle_battery_saver", "prevent_sleep",
      "manual_ips", "button_hints",
      "confirm_exit", "diagnostics_mode", "last_group_id", "last_room_uuid",
      "cached_ips", "playlist_context_group_id", "playlist_context_title",
      "playlist_context_object_id", "playlist_context_artwork_uri",
      "playlist_context_queue_fingerprint"};
  std::vector<std::string> known_with_buttons = known;
  for (std::size_t index = 0; index < kPhysicalButtonCount; ++index) {
    known_with_buttons.push_back(
        std::string("button_") +
        physical_button_id(static_cast<PhysicalButton>(index)));
  }
  for (const auto& field : fields) {
    if (std::find(known_with_buttons.begin(), known_with_buttons.end(),
                  field.first) == known_with_buttons.end()) {
      settings.unknown_fields[field.first] = field.second;
    }
  }
  validate_settings(settings);
  return settings;
}

bool SettingsStore::save(const Settings& input, std::string* error) const {
  Settings settings = input;
  validate_settings(settings);
  std::error_code ec;
  fs::create_directories(directory_, ec);
  if (ec) {
    if (error) *error = "Could not create the Miyonos data directory.";
    return false;
  }
  std::map<std::string, std::string> fields = settings.unknown_fields;
  fields["schema_version"] = "1";
  fields["startup_mode"] =
      settings.startup_mode == StartupMode::SpecificRoom
          ? "specific"
          : settings.startup_mode == StartupMode::AskEveryTime ? "ask" : "last";
  fields["startup_room_uuid"] = settings.startup_room_uuid;
  fields["volume_step"] = std::to_string(settings.volume_step);
  fields["seek_seconds"] = std::to_string(settings.seek_seconds);
  fields["artwork_cache_mb"] = std::to_string(settings.artwork_cache_mb);
  fields["auto_artwork"] = settings.auto_artwork ? "1" : "0";
  fields["spotify_https_artwork"] =
      settings.spotify_https_artwork ? "1" : "0";
  fields["official_sonos_product_photos"] =
      settings.official_sonos_product_photos ? "1" : "0";
  fields["content_defaults_version"] = "1";
  fields["polling"] = settings.polling == PollingIntensity::BatterySaver
                          ? "battery"
                          : settings.polling == PollingIntensity::Responsive
                                ? "responsive"
                                : "balanced";
  fields["dim_timeout_seconds"] =
      std::to_string(settings.dim_timeout_seconds);
  fields["idle_battery_saver"] = settings.idle_battery_saver ? "1" : "0";
  fields["prevent_sleep"] = settings.prevent_sleep ? "1" : "0";
  fields["manual_ips"] = join(settings.manual_ips);
  fields["button_hints"] = settings.button_hints == ButtonHints::Always
                               ? "always"
                               : settings.button_hints == ButtonHints::Never
                                     ? "never"
                                     : "briefly";
  fields["confirm_exit"] = settings.confirm_exit ? "1" : "0";
  fields["diagnostics_mode"] = settings.diagnostics_mode ? "1" : "0";
  fields["last_group_id"] = settings.last_group_id;
  fields["last_room_uuid"] = settings.last_room_uuid;
  fields["cached_ips"] = join(settings.cached_ips);
  fields["playlist_context_group_id"] = settings.playlist_context_group_id;
  fields["playlist_context_title"] = settings.playlist_context_title;
  fields["playlist_context_object_id"] = settings.playlist_context_object_id;
  fields["playlist_context_artwork_uri"] =
      settings.playlist_context_artwork_uri;
  fields["playlist_context_queue_fingerprint"] =
      settings.playlist_context_queue_fingerprint;
  for (std::size_t index = 0; index < kPhysicalButtonCount; ++index) {
    const auto button = static_cast<PhysicalButton>(index);
    fields[std::string("button_") + physical_button_id(button)] =
        action_id(settings.button_mapping[index]);
  }

  const std::string destination = path();
  const std::string temporary =
      (fs::path(directory_) /
       ("settings.ini.tmp." + std::to_string(static_cast<long long>(getpid()))))
          .string();
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      if (error) *error = "Could not write settings.";
      return false;
    }
    output << "# Miyonos settings. Values are stored locally on the SD card.\n";
    for (const auto& field : fields) {
      if (field.first.find_first_of("=\r\n") == std::string::npos &&
          safe_value(field.second)) {
        output << field.first << '=' << field.second << '\n';
      }
    }
    output.flush();
    if (!output) {
      if (error) *error = "Could not finish writing settings.";
      output.close();
      std::remove(temporary.c_str());
      return false;
    }
  }
  if (std::rename(temporary.c_str(), destination.c_str()) != 0) {
    if (error) *error = "Could not replace settings atomically.";
    std::remove(temporary.c_str());
    return false;
  }
  return true;
}

bool SettingsStore::reset(std::string* error) const {
  std::error_code ec;
  fs::remove(path(), ec);
  if (ec) {
    if (error) *error = "Could not reset settings.";
    return false;
  }
  return true;
}

}  // namespace miyonos
