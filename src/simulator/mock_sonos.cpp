#include "simulator/mock_sonos.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <utility>

namespace miyonos {

namespace {

constexpr const char* kAv = "urn:schemas-upnp-org:service:AVTransport:1";
constexpr const char* kRc = "urn:schemas-upnp-org:service:RenderingControl:1";
constexpr const char* kGrc =
    "urn:schemas-upnp-org:service:GroupRenderingControl:1";
constexpr const char* kZgt =
    "urn:schemas-upnp-org:service:ZoneGroupTopology:1";
constexpr const char* kCd =
    "urn:schemas-upnp-org:service:ContentDirectory:1";

std::string xml_escape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + value.size() / 8);
  for (char character : value) {
    switch (character) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '\"': escaped += "&quot;"; break;
      case '\'': escaped += "&apos;"; break;
      default: escaped += character; break;
    }
  }
  return escaped;
}

std::string field(const std::string& body, const std::string& name,
                  const std::string& fallback = {}) {
  const std::string open = "<" + name + ">";
  const std::string close = "</" + name + ">";
  const std::size_t begin = body.find(open);
  if (begin == std::string::npos) return fallback;
  const std::size_t content = begin + open.size();
  const std::size_t end = body.find(close, content);
  if (end == std::string::npos) return fallback;
  return body.substr(content, end - content);
}

int integer_field(const std::string& body, const std::string& name,
                  int fallback) {
  try {
    return std::stoi(field(body, name, std::to_string(fallback)));
  } catch (...) {
    return fallback;
  }
}

std::string envelope(const std::string& action, const std::string& service,
                     const std::string& fields = {}) {
  return "<?xml version=\"1.0\"?>"
         "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
         "<s:Body><u:" +
         action + "Response xmlns:u=\"" + service + "\">" + fields +
         "</u:" + action + "Response></s:Body></s:Envelope>";
}

std::string service(const char* type, const char* id, const char* control) {
  return std::string("<service><serviceType>") + type +
         "</serviceType><serviceId>urn:upnp-org:serviceId:" + id +
         "</serviceId><controlURL>" + control +
         "</controlURL><eventSubURL>" + control +
         "/Event</eventSubURL><SCPDURL>/xml/" + id +
         "1.xml</SCPDURL></service>";
}

std::string didl_item(int identifier) {
  const std::string number = std::to_string(identifier);
  return "<item id=\"" + number +
         "\" parentID=\"Q:0\" restricted=\"true\">"
         "<dc:title>Mock Track " +
         number +
         "</dc:title><dc:creator>Miyonos Ensemble</dc:creator>"
         "<upnp:album>Local Fixtures</upnp:album>"
         "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
         "<upnp:albumArtURI>/getaa?s=1&amp;u=mock</upnp:albumArtURI>"
         "<res duration=\"0:03:42\">http://127.0.0.1/audio/" + number +
         ".mp3</res></item>";
}

std::string didl_playlist_track(int playlist_id, int identifier) {
  const std::string number = std::to_string(identifier);
  const std::string prefix =
      playlist_id == 2 ? "Road Trip Track " : "Mock Track ";
  return "<item id=\"" + number +
         "\" parentID=\"SQ:" + std::to_string(playlist_id) +
         "\" restricted=\"true\">"
         "<dc:title>" + prefix + number +
         "</dc:title><dc:creator>Miyonos Ensemble</dc:creator>"
         "<upnp:album>" +
         (playlist_id == 2 ? "Road Trip Playlist" : "Weekend Playlist") +
         "</upnp:album><upnp:class>object.item.audioItem.musicTrack</upnp:class>"
         "<upnp:albumArtURI>/getaa?s=1&amp;u=mock-" +
         std::to_string(playlist_id) +
         "</upnp:albumArtURI><res duration=\"0:03:42\">"
         "http://127.0.0.1/audio/" + number + ".mp3</res></item>";
}

std::string didl_saved_playlist(int identifier) {
  const std::string title =
      identifier == 2 ? "Road Trip Playlist" : "Weekend Playlist";
  return "<container id=\"SQ:" + std::to_string(identifier) +
         "\" parentID=\"SQ:\"><dc:title>" + title +
         "</dc:title><upnp:albumArtURI>/getaa?s=1&amp;u=mock-" +
         std::to_string(identifier) +
         "</upnp:albumArtURI><upnp:class>object.container.playlistContainer"
         "</upnp:class><res>file:///jffs/settings/savedqueues.rsq#" +
         std::to_string(identifier) + "</res></container>";
}

int saved_playlist_id(const std::string& uri) {
  const std::size_t marker = uri.rfind('#');
  if (marker == std::string::npos) return 0;
  try {
    const int identifier = std::stoi(uri.substr(marker + 1));
    return identifier == 1 || identifier == 2 ? identifier : 0;
  } catch (...) {
    return 0;
  }
}

std::string didl_document(const std::string& items) {
  return "<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
         "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
         "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">" +
         items + "</DIDL-Lite>";
}

void append_u32(std::string& bytes, std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xff));
  }
}

std::uint32_t crc32(const std::string& value) {
  std::uint32_t crc = 0xffffffffU;
  for (unsigned char byte : value) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return crc ^ 0xffffffffU;
}

void append_png_chunk(std::string& png, const char* type,
                      const std::string& data) {
  append_u32(png, static_cast<std::uint32_t>(data.size()));
  const std::string checked = std::string(type, 4) + data;
  png += checked;
  append_u32(png, crc32(checked));
}

std::string simulator_cover_png() {
  constexpr int width = 96;
  constexpr int height = 96;
  std::string pixels;
  pixels.reserve(height * (1 + width * 3));
  for (int y = 0; y < height; ++y) {
    pixels.push_back('\0');
    for (int x = 0; x < width; ++x) {
      unsigned red = 7;
      unsigned green = 20;
      unsigned blue = 43;
      const bool border = x < 5 || x >= width - 5 || y < 5 || y >= height - 5;
      const int dx = x - 48;
      const int dy = y - 49;
      const int radius_squared = dx * dx + dy * dy;
      if (((x + y) / 12) % 2 == 0) {
        red = 15;
        green = 39;
        blue = 70;
      }
      if (radius_squared < 33 * 33) {
        red = 255;
        green = 115;
        blue = 84;
      }
      if (radius_squared < 11 * 11) {
        red = 7;
        green = 29;
        blue = 59;
      }
      if ((x >= 20 && x < 27 && y >= 18 && y < 55) ||
          (x >= 69 && x < 76 && y >= 38 && y < 75)) {
        red = 113;
        green = 214;
        blue = 177;
      }
      if (border) {
        red = 255;
        green = 241;
        blue = 207;
      }
      pixels.push_back(static_cast<char>(red));
      pixels.push_back(static_cast<char>(green));
      pixels.push_back(static_cast<char>(blue));
    }
  }

  std::uint32_t adler_a = 1;
  std::uint32_t adler_b = 0;
  for (unsigned char byte : pixels) {
    adler_a = (adler_a + byte) % 65521;
    adler_b = (adler_b + adler_a) % 65521;
  }
  const std::uint16_t length = static_cast<std::uint16_t>(pixels.size());
  std::string compressed{"\x78\x01", 2};
  compressed.push_back('\x01');
  compressed.push_back(static_cast<char>(length & 0xff));
  compressed.push_back(static_cast<char>((length >> 8) & 0xff));
  const std::uint16_t inverted = static_cast<std::uint16_t>(~length);
  compressed.push_back(static_cast<char>(inverted & 0xff));
  compressed.push_back(static_cast<char>((inverted >> 8) & 0xff));
  compressed += pixels;
  append_u32(compressed, (adler_b << 16) | adler_a);

  std::string png{"\x89PNG\r\n\x1a\n", 8};
  std::string header;
  append_u32(header, width);
  append_u32(header, height);
  header += std::string{"\x08\x02\x00\x00\x00", 5};
  append_png_chunk(png, "IHDR", header);
  append_png_chunk(png, "IDAT", compressed);
  append_png_chunk(png, "IEND", {});
  return png;
}

std::string action_from_headers(const std::string& headers) {
  const std::size_t marker = headers.find('#');
  if (marker == std::string::npos) return {};
  std::size_t end = headers.find_first_of("\"'\r\n", marker + 1);
  if (end == std::string::npos) end = headers.size();
  return headers.substr(marker + 1, end - marker - 1);
}

bool send_all(int socket, const std::string& data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const ssize_t count =
        send(socket, data.data() + sent, data.size() - sent, 0);
    if (count <= 0) return false;
    sent += static_cast<std::size_t>(count);
  }
  return true;
}

}  // namespace

SimulatorSonosFixture::SimulatorSonosFixture(std::string scenario)
    : scenario_(std::move(scenario)) {}

SimulatorSonosFixture::~SimulatorSonosFixture() { stop(); }

bool SimulatorSonosFixture::start() {
  stop();
  listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_socket_ < 0) {
    error_ = std::string("Could not create local fixture socket: ") +
             std::strerror(errno);
    return false;
  }
  int reuse = 1;
  setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(1400);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&address),
           sizeof(address)) != 0 ||
      listen(listen_socket_, 8) != 0) {
    error_ = std::string("Could not start the local Sonos fixture on port 1400: ") +
             std::strerror(errno);
    close(listen_socket_);
    listen_socket_ = -1;
    return false;
  }
  error_.clear();
  running_ = true;
  thread_ = std::thread(&SimulatorSonosFixture::serve, this);
  return true;
}

void SimulatorSonosFixture::stop() {
  running_ = false;
  if (listen_socket_ >= 0) {
    shutdown(listen_socket_, SHUT_RDWR);
    close(listen_socket_);
    listen_socket_ = -1;
  }
  if (thread_.joinable()) thread_.join();
}

void SimulatorSonosFixture::serve() {
  while (running_) {
    const int client = accept(listen_socket_, nullptr, nullptr);
    if (client < 0) {
      if (!running_) break;
      continue;
    }
    handle_client(client);
    shutdown(client, SHUT_RDWR);
    close(client);
  }
}

void SimulatorSonosFixture::handle_client(int client) {
  std::string request;
  char buffer[8192];
  std::size_t header_end = std::string::npos;
  std::size_t expected = 0;
  while (request.size() < 2 * 1024 * 1024) {
    const ssize_t count = recv(client, buffer, sizeof(buffer), 0);
    if (count <= 0) return;
    request.append(buffer, static_cast<std::size_t>(count));
    header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) continue;
    if (expected == 0) {
      const std::string headers = request.substr(0, header_end);
      const std::string needle = "Content-Length:";
      const std::size_t length_at = headers.find(needle);
      if (length_at != std::string::npos) {
        try {
          expected = static_cast<std::size_t>(
              std::stoul(headers.substr(length_at + needle.size())));
        } catch (...) {
          expected = 0;
        }
      }
    }
    if (request.size() >= header_end + 4 + expected) break;
  }
  if (header_end == std::string::npos) return;
  std::istringstream first_line(request.substr(0, request.find("\r\n")));
  std::string method;
  std::string path;
  first_line >> method >> path;
  const std::string headers = request.substr(0, header_end);
  const std::string body = request.substr(header_end + 4, expected);
  int status = 200;
  std::string content_type = "text/xml; charset=utf-8";
  const std::string payload =
      response_for(method, path, headers, body, status, content_type);
  const char* status_text = status == 200 ? "OK" : "Not Found";
  const std::string response =
      "HTTP/1.1 " + std::to_string(status) + " " + status_text +
      "\r\nContent-Type: " + content_type +
      "\r\nContent-Length: " + std::to_string(payload.size()) +
      "\r\nConnection: close\r\n\r\n" + payload;
  send_all(client, response);
}

std::string SimulatorSonosFixture::response_for(
    const std::string& method, const std::string& path,
    const std::string& headers, const std::string& body, int& status,
    std::string& content_type) {
  if (method == "GET") {
    if (path == "/__simulator__/health") {
      return "<scenario>" + xml_escape(scenario_) + "</scenario>";
    }
    if (path.rfind("/getaa", 0) == 0) {
      if (scenario_ == "no-artwork") {
        status = 404;
        return "Artwork intentionally unavailable";
      }
      content_type = "image/png";
      return simulator_cover_png();
    }
    std::string room;
    std::string uuid;
    if (path == "/xml/device_description.xml") {
      room = "Living Room";
      uuid = "RINCON_LIVING";
    } else if (path == "/xml/kitchen_device_description.xml") {
      room = "Kitchen";
      uuid = "RINCON_KITCHEN";
    } else {
      status = 404;
      return "Missing fixture resource";
    }
    return "<?xml version=\"1.0\"?><root "
           "xmlns=\"urn:schemas-upnp-org:device-1-0\"><URLBase>"
           "http://127.0.0.1:1400</URLBase><device><friendlyName>" +
           room + "</friendlyName><roomName>" + room +
           "</roomName><modelName>Sonos Mock</modelName><modelNumber>M1"
           "</modelNumber><serialNum>00-00</serialNum><softwareVersion>99.0"
           "</softwareVersion><UDN>uuid:" + uuid +
           "</UDN><serviceList>" +
           service(kAv, "AVTransport", "/MediaRenderer/AVTransport/Control") +
           service(kRc, "RenderingControl",
                   "/MediaRenderer/RenderingControl/Control") +
           service(kGrc, "GroupRenderingControl",
                   "/MediaRenderer/GroupRenderingControl/Control") +
           service(kZgt, "ZoneGroupTopology", "/ZoneGroupTopology/Control") +
           service(kCd, "ContentDirectory",
                   "/MediaServer/ContentDirectory/Control") +
           "</serviceList></device></root>";
  }

  if (scenario_ == "slow") {
    std::this_thread::sleep_for(std::chrono::milliseconds(850));
  }
  const std::string action = action_from_headers(headers);
  if (action == "GetZoneGroupState") {
    const std::string living =
        "<ZoneGroupMember UUID=\"RINCON_LIVING\" ZoneName=\"Living Room\" "
        "Location=\"http://127.0.0.1:1400/xml/device_description.xml\" "
        "Invisible=\"0\"/>";
    const std::string kitchen =
        "<ZoneGroupMember UUID=\"RINCON_KITCHEN\" ZoneName=\"Kitchen\" "
        "Location=\"http://127.0.0.1:1400/xml/kitchen_device_description.xml\" "
        "Invisible=\"0\"/>";
    std::string topology;
    if (scenario_ == "normal") {
      topology = "<ZoneGroups><ZoneGroup Coordinator=\"RINCON_LIVING\" "
                 "ID=\"RINCON_LIVING:1\">" + living +
                 "</ZoneGroup></ZoneGroups>";
    } else if (scenario_ == "multi-room") {
      topology = "<ZoneGroups><ZoneGroup Coordinator=\"RINCON_LIVING\" "
                 "ID=\"RINCON_LIVING:1\">" + living +
                 "</ZoneGroup><ZoneGroup Coordinator=\"RINCON_KITCHEN\" "
                 "ID=\"RINCON_KITCHEN:2\">" + kitchen +
                 "</ZoneGroup></ZoneGroups>";
    } else {
      const std::string coordinator =
          scenario_ == "coordinator-change" && coordinator_changed_
              ? "RINCON_KITCHEN"
              : "RINCON_LIVING";
      topology = "<ZoneGroups><ZoneGroup Coordinator=\"" + coordinator +
                 "\" ID=\"RINCON_LIVING:1\">" + living + kitchen +
                 "</ZoneGroup></ZoneGroups>";
      if (scenario_ == "coordinator-change") coordinator_changed_ = true;
    }
    return envelope(action, kZgt,
                    "<ZoneGroupState>" + xml_escape(topology) +
                        "</ZoneGroupState>");
  }
  if (action == "GetTransportInfo") {
    return envelope(action, kAv,
                    std::string("<CurrentTransportState>") +
                        (playing_ ? "PLAYING" : "PAUSED_PLAYBACK") +
                        "</CurrentTransportState><CurrentTransportStatus>OK"
                        "</CurrentTransportStatus><CurrentSpeed>1</CurrentSpeed>");
  }
  if (action == "GetPositionInfo") {
    const std::string item =
        "<item id=\"1\"><dc:title>A Local Song &amp; Test</dc:title>"
        "<dc:creator>Miyonos Ensemble</dc:creator>"
        "<upnp:album>Protocol Fixtures</upnp:album>"
        "<res duration=\"0:03:42\">x-rincon-queue:RINCON_LIVING#0</res>"
        "</item>";
    return envelope(action, kAv,
                    "<Track>1</Track><TrackDuration>0:03:42</TrackDuration>"
                    "<TrackMetaData>" + xml_escape(didl_document(item)) +
                    "</TrackMetaData><TrackURI>x-rincon-queue:RINCON_LIVING#0"
                    "</TrackURI><RelTime>0:01:17</RelTime><AbsTime>"
                    "NOT_IMPLEMENTED</AbsTime><RelCount>2147483647</RelCount>"
                    "<AbsCount>2147483647</AbsCount>");
  }
  if (action == "GetMediaInfo") {
    const std::string playlist = didl_saved_playlist(
        loaded_playlist_id_ == 0 ? 1 : loaded_playlist_id_);
    return envelope(action, kAv,
                    "<NrTracks>8</NrTracks><MediaDuration>0:29:00"
                    "</MediaDuration><CurrentURI>x-rincon-queue:RINCON_LIVING#0"
                    "</CurrentURI><CurrentURIMetaData>" +
                        xml_escape(didl_document(playlist)) +
                        "</CurrentURIMetaData>"
                    "<NextURI></NextURI><NextURIMetaData></NextURIMetaData>");
  }
  if (action == "GetCurrentTransportActions") {
    return envelope(action, kAv,
                    "<Actions>Set,Stop,Pause,Play,Seek,Next,Previous</Actions>");
  }
  if (action == "GetVolume" || action == "GetGroupVolume") {
    return envelope(action, action == "GetVolume" ? kRc : kGrc,
                    "<CurrentVolume>" + std::to_string(volume_) +
                        "</CurrentVolume>");
  }
  if (action == "GetMute" || action == "GetGroupMute") {
    return envelope(action, action == "GetMute" ? kRc : kGrc,
                    std::string("<CurrentMute>") + (muted_ ? "1" : "0") +
                        "</CurrentMute>");
  }
  if (action == "SetVolume" || action == "SetGroupVolume") {
    volume_ = std::max(0, std::min(100, integer_field(body, "DesiredVolume", 0)));
    return envelope(action, action == "SetVolume" ? kRc : kGrc);
  }
  if (action == "SetMute" || action == "SetGroupMute") {
    muted_ = field(body, "DesiredMute", "0") == "1";
    return envelope(action, action == "SetMute" ? kRc : kGrc);
  }
  if (action == "Browse") {
    const std::string object_id = field(body, "ObjectID", "Q:");
    const int start = std::max(0, integer_field(body, "StartingIndex", 0));
    const int requested =
        std::max(1, std::min(100, integer_field(body, "RequestedCount", 60)));
    std::string items;
    int total = 0;
    int count = 0;
    if (object_id.rfind("Q", 0) == 0) {
      total = loaded_playlist_id_ == 0
                  ? (scenario_ == "long-queue" ? 360 : 135)
                  : 8;
      for (int index = start + 1;
           index <= total && index <= start + requested; ++index) {
        items += loaded_playlist_id_ == 0
                     ? didl_item(index)
                     : didl_playlist_track(loaded_playlist_id_, index);
        ++count;
      }
    } else if (object_id == "SQ:1" || object_id == "SQ:2") {
      total = 8;
      for (int index = start + 1;
           index <= total && index <= start + requested; ++index) {
        items += didl_playlist_track(object_id == "SQ:2" ? 2 : 1, index);
        ++count;
      }
    } else if (object_id == "FV:2") {
      items = "<container id=\"FV:2/1\" parentID=\"FV:2\">"
              "<dc:title>Morning Collection</dc:title>"
              "<upnp:class>object.container</upnp:class></container>" +
              didl_item(2);
      total = count = 2;
    } else if (object_id == "SQ:") {
      items = didl_saved_playlist(1) + didl_saved_playlist(2);
      total = count = 2;
    }
    return envelope(action, kCd,
                    "<Result>" + xml_escape(didl_document(items)) +
                        "</Result><NumberReturned>" + std::to_string(count) +
                        "</NumberReturned><TotalMatches>" +
                        std::to_string(total) +
                        "</TotalMatches><UpdateID>7</UpdateID>");
  }
  if (action == "AddURIToQueue") {
    const int playlist_id = saved_playlist_id(field(body, "EnqueuedURI"));
    if (playlist_id > 0 && queue_cleared_) {
      loaded_playlist_id_ = playlist_id;
      return envelope(action, kAv,
                      "<FirstTrackNumberEnqueued>1</FirstTrackNumberEnqueued>"
                      "<NumTracksAdded>8</NumTracksAdded>"
                      "<NewQueueLength>8</NewQueueLength>");
    }
    return envelope(action, kAv,
                    "<FirstTrackNumberEnqueued>4</FirstTrackNumberEnqueued>"
                    "<NumTracksAdded>3</NumTracksAdded>"
                    "<NewQueueLength>11</NewQueueLength>");
  }
  if (action == "RemoveAllTracksFromQueue") {
    queue_cleared_ = true;
    loaded_playlist_id_ = 0;
    return envelope(action, kAv);
  }
  if (action == "Pause" || action == "Stop") playing_ = false;
  if (action == "Play") playing_ = true;
  return envelope(action.empty() ? "Unknown" : action, kAv);
}

}  // namespace miyonos
