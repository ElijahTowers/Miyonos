#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "domain/types.h"
#include "network/http.h"

namespace miyonos {

struct SsdpResponse {
  std::string location;
  std::string usn;
  std::string search_target;
  int max_age_seconds = 0;
  std::map<std::string, std::string> headers;
};

struct SoapFault {
  int code = 0;
  std::string description;
};

struct BrowsePage {
  std::vector<BrowseItem> items;
  std::size_t number_returned = 0;
  std::size_t total_matches = 0;
  std::size_t update_id = 0;
};

struct SpeakerVolume {
  int volume = 0;
  bool muted = false;
};

template <typename T>
struct ProtocolResult {
  T value{};
  std::string error;
  int upnp_error_code = 0;

  bool ok() const { return error.empty(); }
};

SsdpResponse parse_ssdp_response(const std::string& response);
std::vector<SsdpResponse> deduplicate_ssdp(
    const std::vector<SsdpResponse>& responses);
ProtocolResult<Player> parse_device_description(const std::string& location,
                                                 const std::string& xml);
ProtocolResult<Topology> parse_topology(const std::string& xml,
                                        const std::vector<Player>& known_players);
TransportState parse_transport_state(const std::string& value);
int clamp_volume(int value);
int parse_duration(const std::string& value);
std::string format_duration(int seconds);
int seek_target(int elapsed_seconds, int delta_seconds, int duration_seconds);
bool is_radio_stream(const Track& track);
bool is_saved_playlist_container(const Track& track);
bool is_playlist_favorite(const BrowseItem& item);
std::string strip_radio_backend_suffix(const std::string& value);
bool is_technical_media_text(const std::string& value);
Track parse_didl_track(const std::string& xml);
ProtocolResult<BrowsePage> parse_browse_response(const std::string& xml);
std::string make_soap_envelope(
    const std::string& service_type, const std::string& action,
    const std::vector<std::pair<std::string, std::string>>& arguments);
SoapFault parse_soap_fault(const std::string& xml);
std::string artwork_url(const Player& player, const std::string& reference,
                        bool allow_external_https = false);

class SonosAdapter {
 public:
  explicit SonosAdapter(std::atomic<bool>* cancelled = nullptr);

  ProtocolResult<std::vector<Player>> discover(
      const std::vector<std::string>& fallback_ips, int timeout_ms = 2600);
  ProtocolResult<Topology> get_topology(const Player& player,
                                        const std::vector<Player>& known_players);
  ProtocolResult<PlaybackSnapshot> get_playback(const Player& coordinator);
  ProtocolResult<SpeakerVolume> get_speaker_volume(const Player& player);
  ProtocolResult<BrowsePage> browse(const Player& coordinator,
                                    const std::string& object_id,
                                    std::size_t start, std::size_t count);

  ProtocolResult<bool> play(const Player& coordinator);
  ProtocolResult<bool> pause(const Player& coordinator);
  ProtocolResult<bool> stop(const Player& coordinator);
  ProtocolResult<bool> next(const Player& coordinator);
  ProtocolResult<bool> previous(const Player& coordinator);
  ProtocolResult<bool> seek_time(const Player& coordinator, int seconds);
  ProtocolResult<bool> play_queue_item(const Player& coordinator,
                                       std::size_t one_based_track);
  ProtocolResult<bool> set_volume(const Player& player, int volume,
                                  bool group);
  ProtocolResult<bool> set_mute(const Player& player, bool muted, bool group);
  ProtocolResult<bool> play_item(const Player& coordinator,
                                 const BrowseItem& item,
                                 bool replace_queue = false);
  ProtocolResult<bool> play_saved_playlist(const Player& coordinator,
                                           const BrowseItem& item);
  ProtocolResult<bool> join_group(const Player& member,
                                  const std::string& coordinator_uuid);
  ProtocolResult<bool> leave_group(const Player& member);

 private:
  ProtocolResult<std::string> soap(
      const Player& player, const std::string& service_name,
      const std::string& action,
      const std::vector<std::pair<std::string, std::string>>& arguments,
      std::size_t max_response = 2 * 1024 * 1024);
  const Service* service(const Player& player,
                         const std::string& service_name) const;
  ProtocolResult<bool> simple_transport(const Player& player,
                                        const std::string& action);
  std::atomic<bool>* cancelled_;
  HttpClient http_;
};

}  // namespace miyonos
