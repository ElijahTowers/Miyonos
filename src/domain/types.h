#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace miyonos {

enum class TransportState {
  Playing,
  Paused,
  Stopped,
  Transitioning,
  NoMedia,
  Unknown
};

struct Service {
  std::string type;
  std::string id;
  std::string control_url;
  std::string event_url;
  std::string scpd_url;
};

struct Player {
  std::string uuid;
  std::string room_name;
  std::string friendly_name;
  std::string model_name;
  std::string model_number;
  std::string serial_number;
  std::string software_version;
  std::string base_url;
  std::string device_description_url;
  std::string ip;
  uint16_t port = 1400;
  bool visible = true;
  bool available = true;
  bool bonded = false;
  std::map<std::string, Service> services;
};

struct Group {
  std::string id;
  std::string coordinator_uuid;
  std::vector<std::string> member_uuids;
  std::string name;
};

struct Track {
  std::string id;
  std::string title;
  std::string artist;
  std::string album;
  std::string station;
  std::string source;
  std::string uri;
  std::string metadata;
  std::string artwork_uri;
  int duration_seconds = 0;
  int elapsed_seconds = 0;
  int track_number = 0;
  int queue_position = 0;
  bool seekable = false;
};

struct BrowseItem {
  std::string id;
  std::string parent_id;
  std::string title;
  std::string artist;
  std::string album;
  std::string uri;
  std::string metadata;
  std::string artwork_uri;
  std::string item_class;
  int duration_seconds = 0;
  bool container = false;
  bool playable = false;
};

struct PlaybackSnapshot {
  TransportState state = TransportState::Unknown;
  Track track;
  std::string playlist_title;
  std::string active_playlist_object_id;
  int volume = 0;
  bool muted = false;
  bool group_volume = false;
  std::string allowed_actions;
  std::string coordinator_uuid;
  uint64_t received_at_ms = 0;
};

struct Topology {
  std::vector<Player> players;
  std::vector<Group> groups;
  std::string household_id;
};

struct DiagnosticState {
  std::string version;
  std::string onion_version;
  std::string local_ip;
  std::string selected_coordinator;
  std::string last_success;
  std::string last_error;
  int last_input_code = 0;
  std::string protocol_version = "local-upnp-v1";
  std::size_t player_count = 0;
  std::size_t cache_bytes = 0;
};

}  // namespace miyonos
