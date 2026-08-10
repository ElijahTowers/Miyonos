#pragma once

#include <map>
#include <string>
#include <vector>

#include "platform/button_mapping.h"

namespace miyonos {

enum class StartupMode { LastUsed, SpecificRoom, AskEveryTime };
enum class PollingIntensity { BatterySaver, Balanced, Responsive };
enum class ButtonHints { Always, Briefly, Never };

struct Settings {
  int schema_version = 1;
  StartupMode startup_mode = StartupMode::LastUsed;
  std::string startup_room_uuid;
  int volume_step = 2;
  int seek_seconds = 10;
  int artwork_cache_mb = 20;
  bool auto_artwork = true;
  bool spotify_https_artwork = true;
  bool official_sonos_product_photos = true;
  PollingIntensity polling = PollingIntensity::Balanced;
  int dim_timeout_seconds = 120;
  bool prevent_sleep = true;
  std::vector<std::string> manual_ips;
  ButtonHints button_hints = ButtonHints::Briefly;
  bool confirm_exit = true;
  bool diagnostics_mode = false;
  ButtonMapping button_mapping = kDefaultButtonMapping;
  std::string last_group_id;
  std::string last_room_uuid;
  std::vector<std::string> cached_ips;
  std::string playlist_context_group_id;
  std::string playlist_context_title;
  std::string playlist_context_object_id;
  std::string playlist_context_artwork_uri;
  std::string playlist_context_queue_fingerprint;
  std::map<std::string, std::string> unknown_fields;
};

class SettingsStore {
 public:
  explicit SettingsStore(std::string data_directory);
  Settings load(std::string* warning = nullptr) const;
  bool save(const Settings& settings, std::string* error = nullptr) const;
  bool reset(std::string* error = nullptr) const;
  std::string path() const;
  const std::string& directory() const { return directory_; }

 private:
  std::string directory_;
};

void validate_settings(Settings& settings);

}  // namespace miyonos
