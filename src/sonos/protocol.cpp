#include "sonos/protocol.h"

#include "network/https_artwork.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <set>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform/clock.h"
#include "platform/logger.h"
#include "sonos/xml.h"

namespace miyonos {

namespace {

constexpr const char* kZonePlayerTarget =
    "urn:schemas-upnp-org:device:ZonePlayer:1";

std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string capped(std::string value, std::size_t limit = 512) {
  value = trim(value);
  if (value.size() > limit) value.resize(limit);
  return value;
}

bool to_size(const std::string& text, std::size_t& value) {
  unsigned long long parsed = 0;
  const auto begin = text.data();
  const auto end = begin + text.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) return false;
  value = static_cast<std::size_t>(parsed);
  return true;
}

int to_int(const std::string& text, int fallback = 0) {
  int value = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} ? value : fallback;
}

bool xml_bool(const std::string& value) {
  const auto lower = lowercase(trim(value));
  return lower == "1" || lower == "true" || lower == "yes";
}

std::string strip_uuid_prefix(std::string value) {
  if (lowercase(value).rfind("uuid:", 0) == 0) value.erase(0, 5);
  return capped(value, 128);
}

// Sonos may rewrite the per-session query parameters of an x-sonosapi-stream
// URI after accepting it. The identifier before '?' is the station identity;
// sid, flags, and sn are transport/session details and are not stable.
std::string radio_stream_identity(const std::string& uri) {
  constexpr char kPrefix[] = "x-sonosapi-stream:";
  std::string identity = lowercase(trim(uri));
  if (identity.rfind(kPrefix, 0) != 0) return {};
  const std::size_t query = identity.find('?', sizeof(kPrefix) - 1);
  if (query != std::string::npos) identity.resize(query);
  return identity.size() > sizeof(kPrefix) - 1 ? identity : std::string{};
}

std::string origin_for(const Url& url) {
  if (!url.valid) return {};
  std::ostringstream result;
  result << "http://" << url.host;
  if (url.port != 80) result << ':' << url.port;
  return result.str();
}

std::string response_value(const std::string& xml, const std::string& name) {
  const auto document = parse_xml(xml);
  if (!document.ok) return {};
  return xml_text(document.root, name);
}

std::string service_key(const std::string& type) {
  const auto marker = type.rfind(":service:");
  if (marker == std::string::npos) return type;
  const auto begin = marker + 9;
  const auto end = type.find(':', begin);
  return end == std::string::npos ? type.substr(begin)
                                  : type.substr(begin, end - begin);
}

void collect_nodes(const XmlNode& node, const std::string& local_name,
                   std::vector<const XmlNode*>& output) {
  if (xml_local_name(node.name) == local_name) output.push_back(&node);
  for (const auto& child : node.children) collect_nodes(child, local_name, output);
}

std::string readable_fault(int code, const std::string& description) {
  if (!description.empty()) return capped(description, 200);
  switch (code) {
    case 701: return "The current source cannot perform that action.";
    case 711: return "Seeking is not supported for this source.";
    case 714: return "That media item cannot be played.";
    case 800: return "The speaker rejected the request.";
    default: return "The speaker returned a protocol error.";
  }
}

std::string item_text(const XmlNode& node, const std::string& name) {
  const auto* found = xml_find(node, name);
  return found ? capped(found->text, 1024) : std::string{};
}

BrowseItem parse_browse_item(const XmlNode& node) {
  BrowseItem item;
  item.container = xml_local_name(node.name) == "container";
  item.id = capped(xml_attribute(node, "id"), 1024);
  item.parent_id = capped(xml_attribute(node, "parentID"), 1024);
  item.title = item_text(node, "title");
  item.artist = item_text(node, "creator");
  if (item.artist.empty()) item.artist = item_text(node, "artist");
  item.album = item_text(node, "album");
  item.artwork_uri = item_text(node, "albumArtURI");
  item.item_class = item_text(node, "class");
  if (const auto* resource = xml_find(node, "res")) {
    item.uri = capped(resource->text, 4096);
    item.duration_seconds = parse_duration(xml_attribute(*resource, "duration"));
  }
  item.metadata = capped(item_text(node, "resMD"), 64 * 1024);
  if (item.metadata.empty() && !item.uri.empty()) {
    const std::string element = item.container ? "container" : "item";
    std::ostringstream metadata;
    metadata << "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
             << "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
             << "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">"
             << '<' << element << " id=\"" << xml_escape(item.id)
             << "\" parentID=\"" << xml_escape(item.parent_id)
             << "\" restricted=\"true\"><dc:title>" << xml_escape(item.title)
             << "</dc:title>";
    if (!item.artist.empty())
      metadata << "<dc:creator>" << xml_escape(item.artist) << "</dc:creator>";
    if (!item.album.empty())
      metadata << "<upnp:album>" << xml_escape(item.album) << "</upnp:album>";
    if (!item.artwork_uri.empty())
      metadata << "<upnp:albumArtURI>" << xml_escape(item.artwork_uri)
               << "</upnp:albumArtURI>";
    if (!item.item_class.empty())
      metadata << "<upnp:class>" << xml_escape(item.item_class)
               << "</upnp:class>";
    metadata << "<res>" << xml_escape(item.uri) << "</res></" << element
             << "></DIDL-Lite>";
    item.metadata = metadata.str();
  }
  item.playable = !item.uri.empty() && !item.container;
  if (item.container && !item.uri.empty() &&
      (item.item_class.find("playlistContainer") != std::string::npos ||
       item.uri.rfind("file:", 0) == 0)) {
    item.playable = true;
  }
  return item;
}

}  // namespace

SsdpResponse parse_ssdp_response(const std::string& response) {
  SsdpResponse result;
  if (response.size() > 64 * 1024) return result;
  std::istringstream stream(response);
  std::string line;
  std::getline(stream, line);
  if (lowercase(line).find("http/1.1 200") == std::string::npos) return result;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    result.headers[lowercase(trim(line.substr(0, colon)))] =
        trim(line.substr(colon + 1));
  }
  result.location = result.headers["location"];
  result.usn = result.headers["usn"];
  result.search_target = result.headers["st"];
  const std::string cache = lowercase(result.headers["cache-control"]);
  const auto age = cache.find("max-age=");
  if (age != std::string::npos) {
    result.max_age_seconds = std::max(0, to_int(cache.substr(age + 8)));
  }
  if (!parse_http_url(result.location).valid) result.location.clear();
  return result;
}

std::vector<SsdpResponse> deduplicate_ssdp(
    const std::vector<SsdpResponse>& responses) {
  std::vector<SsdpResponse> result;
  std::set<std::string> seen;
  for (const auto& response : responses) {
    if (response.location.empty()) continue;
    std::string key = response.usn.empty() ? response.location : response.usn;
    const auto separator = key.find("::");
    if (separator != std::string::npos) key.resize(separator);
    key = lowercase(key);
    if (seen.insert(key).second) result.push_back(response);
  }
  return result;
}

ProtocolResult<Player> parse_device_description(const std::string& location,
                                                 const std::string& xml) {
  ProtocolResult<Player> result;
  const Url location_url = parse_http_url(location);
  if (!location_url.valid) {
    result.error = "Invalid device-description location";
    return result;
  }
  const auto document = parse_xml(xml);
  if (!document.ok) {
    result.error = "Malformed device description";
    return result;
  }
  const auto* device = xml_find(document.root, "device");
  if (!device) {
    result.error = "Device description has no root device";
    return result;
  }
  Player player;
  player.ip = location_url.host;
  player.port = location_url.port;
  std::string base = xml_text(document.root, "URLBase");
  if (!parse_http_url(base).valid) base = origin_for(location_url);
  player.base_url = base;
  player.device_description_url = location;
  player.room_name = capped(xml_text(*device, "roomName"));
  player.friendly_name = capped(xml_text(*device, "friendlyName"));
  if (player.room_name.empty()) player.room_name = player.friendly_name;
  player.model_name = capped(xml_text(*device, "modelName"));
  player.model_number = capped(xml_text(*device, "modelNumber"));
  player.serial_number = capped(xml_text(*device, "serialNum"));
  player.software_version = capped(xml_text(*device, "softwareVersion"));
  player.uuid = strip_uuid_prefix(xml_text(*device, "UDN"));
  std::vector<const XmlNode*> services;
  collect_nodes(*device, "service", services);
  for (const auto* service_node : services) {
    Service service;
    service.type = capped(xml_text(*service_node, "serviceType"), 256);
    service.id = capped(xml_text(*service_node, "serviceId"), 256);
    service.control_url =
        resolve_url(base + "/", xml_text(*service_node, "controlURL"));
    service.event_url =
        resolve_url(base + "/", xml_text(*service_node, "eventSubURL"));
    service.scpd_url = resolve_url(base + "/", xml_text(*service_node, "SCPDURL"));
    if (!service.type.empty() && !service.control_url.empty()) {
      player.services[service_key(service.type)] = std::move(service);
    }
  }
  if (player.uuid.empty() || player.services.empty()) {
    result.error = "Incomplete Sonos device description";
    return result;
  }
  result.value = std::move(player);
  return result;
}

ProtocolResult<Topology> parse_topology(
    const std::string& xml, const std::vector<Player>& known_players) {
  ProtocolResult<Topology> result;
  auto outer = parse_xml(xml);
  if (!outer.ok) {
    result.error = "Malformed topology response";
    return result;
  }
  std::string state;
  if (xml_local_name(outer.root.name) == "ZoneGroups") {
    state = xml;
  } else {
    state = xml_text(outer.root, "ZoneGroupState");
  }
  auto document = parse_xml(state);
  if (!document.ok) {
    result.error = "Malformed zone group state";
    return result;
  }
  result.value.players = known_players;
  std::map<std::string, std::size_t> player_index;
  for (std::size_t i = 0; i < result.value.players.size(); ++i) {
    player_index[result.value.players[i].uuid] = i;
    result.value.players[i].visible = false;
  }
  std::vector<const XmlNode*> groups;
  collect_nodes(document.root, "ZoneGroup", groups);
  for (const auto* group_node : groups) {
    Group group;
    group.id = capped(xml_attribute(*group_node, "ID"), 256);
    group.coordinator_uuid =
        strip_uuid_prefix(xml_attribute(*group_node, "Coordinator"));
    std::vector<const XmlNode*> members;
    for (const auto& child : group_node->children) {
      if (xml_local_name(child.name) == "ZoneGroupMember") members.push_back(&child);
    }
    for (const auto* member : members) {
      const std::string uuid =
          strip_uuid_prefix(xml_attribute(*member, "UUID"));
      if (uuid.empty()) continue;
      const bool zone_bridge =
          xml_bool(xml_attribute(*member, "IsZoneBridge"));
      const bool invisible =
          xml_bool(xml_attribute(*member, "Invisible")) || zone_bridge;
      const std::string zone_name = capped(xml_attribute(*member, "ZoneName"));
      const std::string location = xml_attribute(*member, "Location");
      auto found = player_index.find(uuid);
      if (found == player_index.end()) {
        Player player;
        player.uuid = uuid;
        player.room_name = zone_name;
        player.friendly_name = zone_name;
        player.visible = !invisible;
        const Url member_url = parse_http_url(location);
        if (member_url.valid) {
          player.ip = member_url.host;
          player.port = member_url.port;
          player.base_url = origin_for(member_url);
        }
        found = player_index.emplace(uuid, result.value.players.size()).first;
        result.value.players.push_back(std::move(player));
      }
      Player& player = result.value.players[found->second];
      if (!zone_name.empty()) player.room_name = zone_name;
      player.visible = !invisible;
      if (invisible && !zone_bridge) player.bonded = true;
      player.available = true;
      const Url member_url = parse_http_url(location);
      if (member_url.valid && player.device_description_url.empty()) {
        player.device_description_url = location;
        player.ip = member_url.host;
        player.port = member_url.port;
        player.base_url = origin_for(member_url);
      }
      if (!invisible) group.member_uuids.push_back(uuid);
      for (const auto& child : member->children) {
        if (xml_local_name(child.name) == "Satellite") {
          const std::string satellite_uuid =
              strip_uuid_prefix(xml_attribute(child, "UUID"));
          auto satellite = player_index.find(satellite_uuid);
          if (!satellite_uuid.empty() && satellite == player_index.end()) {
            Player bonded;
            bonded.uuid = satellite_uuid;
            bonded.room_name = zone_name;
            bonded.visible = false;
            bonded.bonded = true;
            satellite =
                player_index.emplace(satellite_uuid,
                                     result.value.players.size())
                    .first;
            result.value.players.push_back(std::move(bonded));
          }
          if (satellite != player_index.end()) {
            result.value.players[satellite->second].visible = false;
            result.value.players[satellite->second].bonded = true;
          }
        }
      }
    }
    if (!group.member_uuids.empty()) {
      for (const auto& uuid : group.member_uuids) {
        const auto found = player_index.find(uuid);
        if (found == player_index.end()) continue;
        if (!group.name.empty()) group.name += " + ";
        group.name += result.value.players[found->second].room_name;
      }
      result.value.groups.push_back(std::move(group));
    }
  }
  if (result.value.groups.empty()) {
    result.error = "Topology contains no visible rooms";
  }
  return result;
}

TransportState parse_transport_state(const std::string& value) {
  if (value == "PLAYING") return TransportState::Playing;
  if (value == "PAUSED_PLAYBACK") return TransportState::Paused;
  if (value == "STOPPED") return TransportState::Stopped;
  if (value == "TRANSITIONING") return TransportState::Transitioning;
  if (value == "NO_MEDIA_PRESENT") return TransportState::NoMedia;
  return TransportState::Unknown;
}

int clamp_volume(int value) { return std::max(0, std::min(100, value)); }

std::string strip_radio_backend_suffix(const std::string& value) {
  const std::string cleaned = trim(value);
  const std::string normalized = lowercase(cleaned);
  const std::size_t broadcast_marker = normalized.rfind("-bb-");
  const std::string codec =
      broadcast_marker == std::string::npos
          ? std::string{}
          : normalized.substr(broadcast_marker + 4);
  if (codec != "aac" && codec != "acc" && codec != "mp3" &&
      codec != "ogg" && codec != "hls" && codec != "flac") {
    return cleaned;
  }
  return trim(cleaned.substr(0, broadcast_marker));
}

bool is_technical_media_text(const std::string& value) {
  const std::string normalized =
      lowercase(strip_radio_backend_suffix(value));
  return normalized.empty() || normalized == "not_implemented" ||
         normalized.rfind("object.", 0) == 0;
}

bool is_radio_stream(const Track& track) {
  const std::string uri = lowercase(trim(track.uri));
  const std::string item_class = lowercase(trim(track.source));
  return uri.rfind("x-sonosapi-stream:", 0) == 0 ||
         uri.rfind("x-sonosapi-hls:", 0) == 0 ||
         uri.rfind("x-rincon-mp3radio:", 0) == 0 ||
         uri.rfind("x-rincon-stream:", 0) == 0 ||
         item_class.find("audiobroadcast") != std::string::npos;
}

int parse_duration(const std::string& value) {
  if (value.empty() || value.size() > 16) return 0;
  std::istringstream stream(value);
  std::string part;
  std::vector<int> parts;
  while (std::getline(stream, part, ':')) {
    if (part.empty() || part.size() > 3) return 0;
    int number = to_int(part, -1);
    if (number < 0) return 0;
    parts.push_back(number);
  }
  if (parts.size() != 3 || parts[1] > 59 || parts[2] > 59 ||
      parts[0] > 999) {
    return 0;
  }
  return parts[0] * 3600 + parts[1] * 60 + parts[2];
}

std::string format_duration(int seconds) {
  seconds = std::max(0, std::min(seconds, 999 * 3600 + 3599));
  std::ostringstream out;
  out << (seconds / 3600) << ':';
  const int minutes = (seconds / 60) % 60;
  const int remainder = seconds % 60;
  if (minutes < 10) out << '0';
  out << minutes << ':';
  if (remainder < 10) out << '0';
  out << remainder;
  return out.str();
}

int seek_target(int elapsed_seconds, int delta_seconds, int duration_seconds) {
  if (duration_seconds <= 0) return 0;
  const long long target =
      static_cast<long long>(elapsed_seconds) + static_cast<long long>(delta_seconds);
  return static_cast<int>(
      std::max<long long>(0, std::min<long long>(duration_seconds, target)));
}

Track parse_didl_track(const std::string& xml) {
  Track track;
  if (xml.empty() || xml == "NOT_IMPLEMENTED") return track;
  const auto document = parse_xml(xml, 512 * 1024);
  if (!document.ok) return track;
  const XmlNode* item = xml_find(document.root, "item");
  if (!item) item = xml_find(document.root, "container");
  if (!item) return track;
  track.id = capped(xml_attribute(*item, "id"), 1024);
  track.title = item_text(*item, "title");
  track.artist = item_text(*item, "creator");
  if (track.artist.empty()) track.artist = item_text(*item, "artist");
  track.album = item_text(*item, "album");
  track.station = item_text(*item, "radioShowMd");
  track.artwork_uri = item_text(*item, "albumArtURI");
  track.source = item_text(*item, "class");
  if (const auto* resource = xml_find(*item, "res")) {
    track.uri = capped(resource->text, 4096);
    track.duration_seconds = parse_duration(xml_attribute(*resource, "duration"));
  }
  return track;
}

bool is_saved_playlist_container(const Track& track) {
  const std::string source = lowercase(track.source);
  const std::string uri = lowercase(track.uri);
  const std::string id = lowercase(track.id);
  return id.rfind("sq:", 0) == 0 ||
         source.find("playlistcontainer") != std::string::npos ||
         uri.find("savedqueues.rsq") != std::string::npos;
}

namespace {

void merge_missing_media_metadata(Track& track, const Track& media_track) {
  if (track.title.empty()) track.title = media_track.title;
  if (track.artist.empty()) track.artist = media_track.artist;
  if (track.album.empty()) track.album = media_track.album;
  if (track.station.empty()) track.station = media_track.station;
  if (track.source.empty()) track.source = media_track.source;
  if (track.uri.empty()) track.uri = media_track.uri;
  if (track.artwork_uri.empty()) track.artwork_uri = media_track.artwork_uri;
}

}  // namespace

ProtocolResult<BrowsePage> parse_browse_response(const std::string& xml) {
  ProtocolResult<BrowsePage> result;
  const auto document = parse_xml(xml);
  if (!document.ok) {
    result.error = "Malformed browse response";
    return result;
  }
  const std::string didl = xml_text(document.root, "Result");
  const auto items = parse_xml(didl, 2 * 1024 * 1024, 8192, 32);
  if (!items.ok) {
    result.error = "Malformed media listing";
    return result;
  }
  for (const auto& node : items.root.children) {
    const auto local = xml_local_name(node.name);
    if (local == "item" || local == "container") {
      BrowseItem item = parse_browse_item(node);
      // Sonos can return a visual navigation placeholder in Favorites with a
      // title but neither a child container nor a playable resource. It is
      // not an item the user can open or start, so do not expose it as one.
      if (item.container || item.playable) {
        result.value.items.push_back(std::move(item));
      }
    }
  }
  to_size(xml_text(document.root, "NumberReturned"),
          result.value.number_returned);
  to_size(xml_text(document.root, "TotalMatches"), result.value.total_matches);
  to_size(xml_text(document.root, "UpdateID"), result.value.update_id);
  return result;
}

std::string make_soap_envelope(
    const std::string& service_type, const std::string& action,
    const std::vector<std::pair<std::string, std::string>>& arguments) {
  std::ostringstream body;
  body << "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
       << "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
       << "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
       << "<s:Body><u:" << action << " xmlns:u=\"" << xml_escape(service_type)
       << "\">";
  for (const auto& argument : arguments) {
    body << '<' << argument.first << '>' << xml_escape(argument.second) << "</"
         << argument.first << '>';
  }
  body << "</u:" << action << "></s:Body></s:Envelope>";
  return body.str();
}

SoapFault parse_soap_fault(const std::string& xml) {
  SoapFault fault;
  const auto document = parse_xml(xml);
  if (!document.ok || !xml_find(document.root, "Fault")) return fault;
  fault.code = to_int(xml_text(document.root, "errorCode"));
  fault.description = capped(xml_text(document.root, "errorDescription"), 200);
  if (fault.description.empty()) {
    fault.description = capped(xml_text(document.root, "faultstring"), 200);
  }
  return fault;
}

std::string artwork_url(const Player& player, const std::string& reference,
                        bool allow_external_https) {
  if (reference.empty()) return {};
  if (parse_http_url(reference).valid) return reference;
  if (allow_external_https && is_trusted_external_artwork_url(reference)) {
    return reference;
  }
  return resolve_url(player.base_url + "/", reference);
}

SonosAdapter::SonosAdapter(std::atomic<bool>* cancelled)
    : cancelled_(cancelled), http_(cancelled) {}

ProtocolResult<std::vector<Player>> SonosAdapter::discover(
    const std::vector<std::string>& fallback_ips, int timeout_ms) {
  ProtocolResult<std::vector<Player>> result;
  std::vector<SsdpResponse> responses;
  const bool disable_ssdp = std::getenv("MIYONOS_DISABLE_SSDP") != nullptr;
  const int fd = disable_ssdp ? -1 : socket(AF_INET, SOCK_DGRAM, 0);
  if (fd >= 0) {
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int broadcast = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    const std::string message =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n\r\n";
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &destination.sin_addr);
    sendto(fd, message.data(), message.size(), 0,
           reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
    inet_pton(AF_INET, "255.255.255.255", &destination.sin_addr);
    sendto(fd, message.data(), message.size(), 0,
           reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
    const auto deadline = monotonic_ms() + static_cast<uint64_t>(timeout_ms);
    while ((!cancelled_ || !cancelled_->load()) && monotonic_ms() < deadline) {
      fd_set read_set;
      FD_ZERO(&read_set);
      FD_SET(fd, &read_set);
      timeval timeout{0, 100000};
      const int ready = select(fd + 1, &read_set, nullptr, nullptr, &timeout);
      if (ready <= 0) continue;
      char buffer[65536];
      const auto count = recv(fd, buffer, sizeof(buffer), 0);
      if (count > 0) {
        auto parsed = parse_ssdp_response(
            std::string(buffer, static_cast<std::size_t>(count)));
        if (!parsed.location.empty() &&
            (parsed.search_target == kZonePlayerTarget ||
             parsed.usn.find("ZonePlayer") != std::string::npos)) {
          responses.push_back(std::move(parsed));
        }
      }
    }
    close(fd);
  }
  for (const auto& ip : fallback_ips) {
    if (!valid_ipv4(ip)) continue;
    SsdpResponse fallback;
    fallback.location = "http://" + ip + ":1400/xml/device_description.xml";
    fallback.usn = ip;
    responses.push_back(std::move(fallback));
  }
  std::set<std::string> uuids;
  for (const auto& response : deduplicate_ssdp(responses)) {
    HttpClient::Limits limits;
    limits.max_body_bytes = 1024 * 1024;
    const auto description = http_.get(response.location, limits);
    if (!description.ok()) {
      MIYONOS_VERBOSE("discovery", "Description failed for " + response.location);
      continue;
    }
    auto player = parse_device_description(response.location, description.body);
    if (player.ok() && uuids.insert(player.value.uuid).second) {
      result.value.push_back(std::move(player.value));
    }
  }
  if (result.value.empty()) {
    result.error = "No Sonos system was found on this Wi-Fi network.";
  }
  return result;
}

const Service* SonosAdapter::service(const Player& player,
                                     const std::string& service_name) const {
  const auto found = player.services.find(service_name);
  if (found != player.services.end()) return &found->second;
  for (const auto& entry : player.services) {
    if (entry.second.type.find(":service:" + service_name + ":") !=
        std::string::npos) {
      return &entry.second;
    }
  }
  return nullptr;
}

ProtocolResult<std::string> SonosAdapter::soap(
    const Player& player, const std::string& service_name,
    const std::string& action,
    const std::vector<std::pair<std::string, std::string>>& arguments,
    std::size_t max_response) {
  ProtocolResult<std::string> result;
  const Service* target = service(player, service_name);
  if (!target || !parse_http_url(target->control_url).valid) {
    result.error = service_name + " is not supported by this player.";
    MIYONOS_VERBOSE("sonos", action + " skipped: " + result.error);
    return result;
  }
  MIYONOS_VERBOSE("sonos", service_name + ":" + action + " -> " +
                                  capped(player.room_name, 96));
  const std::string body = make_soap_envelope(target->type, action, arguments);
  HttpClient::Limits limits;
  limits.max_body_bytes = max_response;
  const auto response =
      http_.post(target->control_url, "text/xml; charset=\"utf-8\"", body,
                 {{"SOAPACTION", "\"" + target->type + "#" + action + "\""}},
                 limits);
  if (!response.error.empty()) {
    result.error = response.error;
    MIYONOS_VERBOSE("sonos", service_name + ":" + action +
                                    " transport failure: " + result.error);
    return result;
  }
  if (!response.ok()) {
    const auto fault = parse_soap_fault(response.body);
    result.upnp_error_code = fault.code;
    result.error = readable_fault(fault.code, fault.description);
    MIYONOS_VERBOSE("sonos", service_name + ":" + action +
                                    " SOAP fault " +
                                    std::to_string(result.upnp_error_code));
    return result;
  }
  const auto fault = parse_soap_fault(response.body);
  if (fault.code != 0) {
    result.upnp_error_code = fault.code;
    result.error = readable_fault(fault.code, fault.description);
    MIYONOS_VERBOSE("sonos", service_name + ":" + action +
                                    " embedded fault " +
                                    std::to_string(result.upnp_error_code));
    return result;
  }
  result.value = response.body;
  return result;
}

ProtocolResult<Topology> SonosAdapter::get_topology(
    const Player& player, const std::vector<Player>& known_players) {
  auto response = soap(player, "ZoneGroupTopology", "GetZoneGroupState", {});
  if (!response.ok()) return {{}, response.error, response.upnp_error_code};
  auto topology = parse_topology(response.value, known_players);
  if (!topology.ok()) return topology;
  for (auto& member : topology.value.players) {
    if (!member.visible || !member.services.empty() ||
        !parse_http_url(member.device_description_url).valid) {
      continue;
    }
    HttpClient::Limits limits;
    limits.max_body_bytes = 512 * 1024;
    const auto description = http_.get(member.device_description_url, limits);
    if (!description.ok()) {
      member.available = false;
      continue;
    }
    auto detailed =
        parse_device_description(member.device_description_url, description.body);
    if (!detailed.ok() || detailed.value.uuid != member.uuid) {
      member.available = false;
      continue;
    }
    const bool visible = member.visible;
    const bool bonded = member.bonded;
    member = std::move(detailed.value);
    member.visible = visible;
    member.bonded = bonded;
  }
  return topology;
}

ProtocolResult<PlaybackSnapshot> SonosAdapter::get_playback(
    const Player& coordinator) {
  ProtocolResult<PlaybackSnapshot> result;
  result.value.coordinator_uuid = coordinator.uuid;
  const auto transport =
      soap(coordinator, "AVTransport", "GetTransportInfo", {{"InstanceID", "0"}});
  if (!transport.ok()) return {{}, transport.error, transport.upnp_error_code};
  result.value.state =
      parse_transport_state(response_value(transport.value, "CurrentTransportState"));

  const auto position =
      soap(coordinator, "AVTransport", "GetPositionInfo", {{"InstanceID", "0"}});
  if (position.ok()) {
    result.value.track = parse_didl_track(response_value(position.value, "TrackMetaData"));
    result.value.track.duration_seconds =
        parse_duration(response_value(position.value, "TrackDuration"));
    result.value.track.elapsed_seconds =
        parse_duration(response_value(position.value, "RelTime"));
    result.value.track.queue_position =
        to_int(response_value(position.value, "Track"));
    result.value.track.uri = capped(response_value(position.value, "TrackURI"), 4096);
  }
  const auto media =
      soap(coordinator, "AVTransport", "GetMediaInfo", {{"InstanceID", "0"}});
  if (media.ok()) {
    const Track media_track =
        parse_didl_track(response_value(media.value, "CurrentURIMetaData"));
    if (is_saved_playlist_container(media_track)) {
      result.value.playlist_title = media_track.title;
      result.value.active_playlist_object_id = media_track.id;
    }
    merge_missing_media_metadata(result.value.track, media_track);
  }
  const auto actions = soap(coordinator, "AVTransport",
                            "GetCurrentTransportActions", {{"InstanceID", "0"}});
  if (actions.ok()) {
    result.value.allowed_actions =
        capped(response_value(actions.value, "Actions"), 512);
  }
  result.value.track.seekable =
      result.value.track.duration_seconds > 0 &&
      (result.value.allowed_actions.empty() ||
       result.value.allowed_actions.find("Seek") != std::string::npos);

  const bool group_supported =
      service(coordinator, "GroupRenderingControl") != nullptr;
  auto volume =
      group_supported
          ? soap(coordinator, "GroupRenderingControl", "GetGroupVolume",
                 {{"InstanceID", "0"}})
          : soap(coordinator, "RenderingControl", "GetVolume",
                 {{"InstanceID", "0"}, {"Channel", "Master"}});
  if (volume.ok()) {
    result.value.volume =
        clamp_volume(to_int(response_value(volume.value,
                                           group_supported ? "CurrentVolume"
                                                           : "CurrentVolume")));
    result.value.group_volume = group_supported;
  }
  auto mute = group_supported
                  ? soap(coordinator, "GroupRenderingControl", "GetGroupMute",
                         {{"InstanceID", "0"}})
                  : soap(coordinator, "RenderingControl", "GetMute",
                         {{"InstanceID", "0"}, {"Channel", "Master"}});
  if (mute.ok()) {
    result.value.muted =
        xml_bool(response_value(mute.value,
                                group_supported ? "CurrentMute" : "CurrentMute"));
  }
  result.value.received_at_ms = monotonic_ms();
  return result;
}

ProtocolResult<SpeakerVolume> SonosAdapter::get_speaker_volume(
    const Player& player) {
  ProtocolResult<SpeakerVolume> result;
  const auto volume = soap(player, "RenderingControl", "GetVolume",
                           {{"InstanceID", "0"}, {"Channel", "Master"}});
  if (!volume.ok()) return {{}, volume.error, volume.upnp_error_code};
  result.value.volume = clamp_volume(
      to_int(response_value(volume.value, "CurrentVolume")));
  const auto mute = soap(player, "RenderingControl", "GetMute",
                         {{"InstanceID", "0"}, {"Channel", "Master"}});
  if (mute.ok()) {
    result.value.muted = xml_bool(response_value(mute.value, "CurrentMute"));
  }
  return result;
}

ProtocolResult<BrowsePage> SonosAdapter::browse(
    const Player& coordinator, const std::string& object_id, std::size_t start,
    std::size_t count) {
  count = std::max<std::size_t>(1, std::min<std::size_t>(100, count));
  auto response = soap(
      coordinator, "ContentDirectory", "Browse",
      {{"ObjectID", capped(object_id, 1024)},
       {"BrowseFlag", "BrowseDirectChildren"},
       {"Filter", "*"},
       {"StartingIndex", std::to_string(start)},
       {"RequestedCount", std::to_string(count)},
       {"SortCriteria", ""}});
  if (!response.ok()) return {{}, response.error, response.upnp_error_code};
  return parse_browse_response(response.value);
}

ProtocolResult<bool> SonosAdapter::simple_transport(
    const Player& player, const std::string& action) {
  auto response = soap(player, "AVTransport", action, {{"InstanceID", "0"}});
  if (!response.ok()) return {false, response.error, response.upnp_error_code};
  return {true, {}, 0};
}

ProtocolResult<bool> SonosAdapter::play(const Player& coordinator) {
  auto response = soap(coordinator, "AVTransport", "Play",
                       {{"InstanceID", "0"}, {"Speed", "1"}});
  return {response.ok(), response.error, response.upnp_error_code};
}
ProtocolResult<bool> SonosAdapter::pause(const Player& coordinator) {
  return simple_transport(coordinator, "Pause");
}
ProtocolResult<bool> SonosAdapter::stop(const Player& coordinator) {
  return simple_transport(coordinator, "Stop");
}
ProtocolResult<bool> SonosAdapter::next(const Player& coordinator) {
  return simple_transport(coordinator, "Next");
}
ProtocolResult<bool> SonosAdapter::previous(const Player& coordinator) {
  return simple_transport(coordinator, "Previous");
}

ProtocolResult<bool> SonosAdapter::seek_time(const Player& coordinator,
                                             int seconds) {
  auto response = soap(coordinator, "AVTransport", "Seek",
                       {{"InstanceID", "0"},
                        {"Unit", "REL_TIME"},
                        {"Target", format_duration(seconds)}});
  return {response.ok(), response.error, response.upnp_error_code};
}

ProtocolResult<bool> SonosAdapter::play_queue_item(
    const Player& coordinator, std::size_t one_based_track) {
  one_based_track = std::max<std::size_t>(1, one_based_track);
  auto seek = soap(coordinator, "AVTransport", "Seek",
                   {{"InstanceID", "0"},
                    {"Unit", "TRACK_NR"},
                    {"Target", std::to_string(one_based_track)}});
  if (!seek.ok()) return {false, seek.error, seek.upnp_error_code};
  return play(coordinator);
}

ProtocolResult<bool> SonosAdapter::set_volume(const Player& player, int volume,
                                               bool group) {
  volume = clamp_volume(volume);
  const bool use_group = group && service(player, "GroupRenderingControl");
  auto response =
      use_group
          ? soap(player, "GroupRenderingControl", "SetGroupVolume",
                 {{"InstanceID", "0"},
                  {"DesiredVolume", std::to_string(volume)}})
          : soap(player, "RenderingControl", "SetVolume",
                 {{"InstanceID", "0"},
                  {"Channel", "Master"},
                  {"DesiredVolume", std::to_string(volume)}});
  return {response.ok(), response.error, response.upnp_error_code};
}

ProtocolResult<bool> SonosAdapter::set_mute(const Player& player, bool muted,
                                             bool group) {
  const bool use_group = group && service(player, "GroupRenderingControl");
  auto response =
      use_group
          ? soap(player, "GroupRenderingControl", "SetGroupMute",
                 {{"InstanceID", "0"}, {"DesiredMute", muted ? "1" : "0"}})
          : soap(player, "RenderingControl", "SetMute",
                 {{"InstanceID", "0"},
                  {"Channel", "Master"},
                  {"DesiredMute", muted ? "1" : "0"}});
  return {response.ok(), response.error, response.upnp_error_code};
}

ProtocolResult<bool> SonosAdapter::play_item(const Player& coordinator,
                                              const BrowseItem& item) {
  if (!item.playable || item.uri.empty()) {
    return {false, "This item type is not supported yet.", 0};
  }
  auto failure = [](const std::string& stage,
                    const ProtocolResult<std::string>& response) {
    std::string error = stage + " failed";
    if (!response.error.empty()) error += ": " + response.error;
    if (response.upnp_error_code > 0) {
      error += " (UPnP " + std::to_string(response.upnp_error_code) + ")";
    }
    return ProtocolResult<bool>{false, error, response.upnp_error_code};
  };

  if (item.uri.rfind("x-rincon-cpcontainer:", 0) == 0) {
    auto added = soap(
        coordinator, "AVTransport", "AddURIToQueue",
        {{"InstanceID", "0"},
         {"EnqueuedURI", item.uri},
         {"EnqueuedURIMetaData", item.metadata},
         {"DesiredFirstTrackNumberEnqueued", "0"},
         {"EnqueueAsNext", "1"}});
    if (!added.ok()) return failure("Adding this favorite to the queue", added);
    const int first_track =
        to_int(response_value(added.value, "FirstTrackNumberEnqueued"));
    if (first_track <= 0) {
      return {false, "Sonos did not return the queued favorite position.", 0};
    }

    auto set = soap(coordinator, "AVTransport", "SetAVTransportURI",
                    {{"InstanceID", "0"},
                     {"CurrentURI", "x-rincon-queue:" + coordinator.uuid + "#0"},
                     {"CurrentURIMetaData", ""}});
    if (!set.ok()) return failure("Opening the Sonos queue", set);
    auto seek = soap(coordinator, "AVTransport", "Seek",
                     {{"InstanceID", "0"},
                      {"Unit", "TRACK_NR"},
                      {"Target", std::to_string(first_track)}});
    if (!seek.ok()) return failure("Selecting the queued favorite", seek);
    auto start = soap(coordinator, "AVTransport", "Play",
                      {{"InstanceID", "0"}, {"Speed", "1"}});
    if (!start.ok()) return failure("Starting the queued favorite", start);
    return {true, {}, 0};
  }

  auto set = soap(coordinator, "AVTransport", "SetAVTransportURI",
                  {{"InstanceID", "0"},
                   {"CurrentURI", item.uri},
                   {"CurrentURIMetaData", item.metadata}});
  if (!set.ok()) return failure("Opening this favorite", set);
  auto start = soap(coordinator, "AVTransport", "Play",
                    {{"InstanceID", "0"}, {"Speed", "1"}});
  const bool radio_stream = item.uri.rfind("x-sonosapi-stream:", 0) == 0;
  if (!radio_stream) {
    if (!start.ok()) return failure("Starting this favorite", start);
    return {true, {}, 0};
  }

  // Sonos Radio may answer the immediate Play command with UPnP 701 while it
  // is still accepting the URI and filling the stream buffer. Reconcile that
  // transient response with bounded state reads before showing an error.
  if (!start.ok() && start.upnp_error_code != 701) {
    return failure("Starting this station", start);
  }
  const std::string requested_station = radio_stream_identity(item.uri);
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(8000);
  do {
    const auto transport =
        soap(coordinator, "AVTransport", "GetTransportInfo", {{"InstanceID", "0"}});
    if (transport.ok() &&
        parse_transport_state(response_value(transport.value,
                                             "CurrentTransportState")) ==
            TransportState::Playing) {
      const auto media =
          soap(coordinator, "AVTransport", "GetMediaInfo", {{"InstanceID", "0"}});
      if (media.ok() &&
          radio_stream_identity(response_value(media.value, "CurrentURI")) ==
              requested_station) {
        return {true, {}, 0};
      }
    }
    usleep(250000);
  } while (std::chrono::steady_clock::now() < deadline);
  std::string error = "The station did not begin playback.";
  if (!start.ok() && !start.error.empty()) error += " " + start.error;
  return {false, error, start.upnp_error_code};
}

ProtocolResult<bool> SonosAdapter::play_saved_playlist(
    const Player& coordinator, const BrowseItem& item) {
  const bool is_saved_playlist =
      item.container && item.playable &&
      (item.item_class.find("playlistContainer") != std::string::npos ||
       item.uri.rfind("file:", 0) == 0);
  if (!is_saved_playlist || item.uri.empty()) {
    return {false, "This saved playlist is not supported yet.", 0};
  }
  auto failure = [](const std::string& stage,
                    const ProtocolResult<std::string>& response) {
    std::string error = stage + " failed";
    if (!response.error.empty()) error += ": " + response.error;
    if (response.upnp_error_code > 0) {
      error += " (UPnP " + std::to_string(response.upnp_error_code) + ")";
    }
    return ProtocolResult<bool>{false, error, response.upnp_error_code};
  };

  // Saved queues must replace, not append to, the active Sonos queue. Loading
  // into a clean queue prevents tracks from a previously selected playlist
  // from being played after the new selection.
  auto cleared = soap(coordinator, "AVTransport", "RemoveAllTracksFromQueue",
                      {{"InstanceID", "0"}});
  // Sonos returns UPnP 804 when there is nothing to remove. That is already
  // the required clean-queue state, so playback can continue safely.
  if (!cleared.ok() && cleared.upnp_error_code != 804) {
    return failure("Clearing the current queue", cleared);
  }

  auto added = soap(
      coordinator, "AVTransport", "AddURIToQueue",
      {{"InstanceID", "0"},
       {"EnqueuedURI", item.uri},
       // Sonos saved queues are addressed by their local URI. Supplying
       // generated browse metadata here can make older players reject the
       // otherwise valid saved-queue load.
       {"EnqueuedURIMetaData", ""},
       {"DesiredFirstTrackNumberEnqueued", "0"},
       {"EnqueueAsNext", "0"}});
  if (!added.ok()) return failure("Loading the saved playlist", added);
  const int first_track =
      to_int(response_value(added.value, "FirstTrackNumberEnqueued"));
  if (first_track <= 0) {
    return {false, "Sonos did not return the saved playlist position.", 0};
  }

  auto set = soap(coordinator, "AVTransport", "SetAVTransportURI",
                  {{"InstanceID", "0"},
                   {"CurrentURI", "x-rincon-queue:" + coordinator.uuid + "#0"},
                   {"CurrentURIMetaData", ""}});
  if (!set.ok()) return failure("Opening the updated queue", set);
  auto seek = soap(coordinator, "AVTransport", "Seek",
                   {{"InstanceID", "0"},
                    {"Unit", "TRACK_NR"},
                    {"Target", std::to_string(first_track)}});
  if (!seek.ok()) return failure("Selecting the first playlist track", seek);
  auto start = soap(coordinator, "AVTransport", "Play",
                    {{"InstanceID", "0"}, {"Speed", "1"}});
  if (!start.ok()) return failure("Starting the saved playlist", start);
  return {true, {}, 0};
}

ProtocolResult<bool> SonosAdapter::join_group(
    const Player& member, const std::string& coordinator_uuid) {
  if (coordinator_uuid.empty() || coordinator_uuid == member.uuid) {
    return {false, "A room cannot join itself.", 0};
  }
  auto response = soap(member, "AVTransport", "SetAVTransportURI",
                       {{"InstanceID", "0"},
                        {"CurrentURI", "x-rincon:" + coordinator_uuid},
                        {"CurrentURIMetaData", ""}});
  return {response.ok(), response.error, response.upnp_error_code};
}

ProtocolResult<bool> SonosAdapter::leave_group(const Player& member) {
  auto response = soap(member, "AVTransport",
                       "BecomeCoordinatorOfStandaloneGroup",
                       {{"InstanceID", "0"}});
  return {response.ok(), response.error, response.upnp_error_code};
}

}  // namespace miyonos
