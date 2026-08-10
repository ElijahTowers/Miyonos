#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "app/controller.h"
#include "network/http.h"
#include "network/https_artwork.h"
#include "platform/battery.h"
#include "sonos/protocol.h"
#include "sonos/xml.h"
#include "storage/artwork_cache.h"
#include "storage/settings.h"
#include "util/bounded_queue.h"

namespace fs = std::filesystem;
using namespace miyonos;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      ++failures;                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << " check failed: "           \
                << #condition << '\n';                                         \
    }                                                                          \
  } while (false)

std::string fixture(const std::string& name) {
  fs::path path = fs::path(MIYONOS_SOURCE_DIR) / "tests" / "fixtures" / name;
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), {});
}

std::string temp_directory() {
  fs::path base = fs::temp_directory_path() / "miyonos-tests-XXXXXX";
  std::string value = base.string();
  std::vector<char> writable(value.begin(), value.end());
  writable.push_back('\0');
  char* made = mkdtemp(writable.data());
  return made ? std::string(made) : std::string{};
}

std::string device_description() {
  return R"(<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
<URLBase>http://192.168.1.42:1400</URLBase><device>
<friendlyName>Living Room</friendlyName><roomName>Living Room</roomName>
<modelName>Era 100</modelName><modelNumber>S41</modelNumber>
<serialNum>AA-BB</serialNum><softwareVersion>99.1</softwareVersion>
<UDN>uuid:RINCON_BAR</UDN><serviceList>
<service><serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>
<serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>
<controlURL>/MediaRenderer/AVTransport/Control</controlURL>
<eventSubURL>/MediaRenderer/AVTransport/Event</eventSubURL>
<SCPDURL>/xml/AVTransport1.xml</SCPDURL></service>
<service><serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>
<serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>
<controlURL>/MediaRenderer/RenderingControl/Control</controlURL>
<eventSubURL>/MediaRenderer/RenderingControl/Event</eventSubURL>
<SCPDURL>/xml/RenderingControl1.xml</SCPDURL></service>
</serviceList></device></root>)";
}

void test_ssdp() {
  auto response = parse_ssdp_response(fixture("ssdp_response.txt"));
  CHECK(response.location ==
        "http://192.168.1.42:1400/xml/device_description.xml");
  CHECK(response.usn.find("RINCON_TEST") != std::string::npos);
  CHECK(response.max_age_seconds == 1800);
  auto lower_case = parse_ssdp_response(
      "HTTP/1.1 200 OK\r\nlocation: "
      "http://192.168.1.42:1400/xml/device_description.xml\r\n"
      "UsN: uuid:RINCON_TEST\r\nsT: urn:test\r\n\r\n");
  CHECK(!lower_case.location.empty());
  auto unique = deduplicate_ssdp({response, lower_case, response});
  CHECK(unique.size() == 1);
}

void test_url_and_http_helpers() {
  auto url = parse_http_url("http://192.168.1.42:1400/xml/device.xml?x=1");
  CHECK(url.valid);
  CHECK(url.host == "192.168.1.42");
  CHECK(url.port == 1400);
  CHECK(!parse_http_url("https://192.168.1.42/device").valid);
  CHECK(!parse_http_url("http://example.com/device").valid);
  CHECK(is_trusted_external_artwork_url(
      "https://i.scdn.co/image/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  CHECK(trusted_external_artwork_path(
      "https://i.scdn.co/image/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef") ==
      "/image/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  CHECK(is_trusted_external_artwork_url(
      "https://i.scdn.co/image/ab67706f000000029c46ccbec8d7711edb4afe5e"));
  CHECK(trusted_external_artwork_host(
      "https://i.scdn.co/image/ab67706f000000029c46ccbec8d7711edb4afe5e") ==
      "i.scdn.co");
  CHECK(is_trusted_external_artwork_url(
      "https://mosaic.scdn.co/640/2460494fd59ee33f74c582e2f00f212a2ccb847e901c946629df19e43498568ccddc611b780ecad6d98df12f96bd9cea4ba76a1b8062821e570b5f29fd0337f6d10f7e0d0c0acfa86a6e8717b0335753"));
  CHECK(is_trusted_external_artwork_url(
      "https://seed-mix-image.spotifycdn.com/v6/img/desc/Pop%20Soul/en/default"));
  CHECK(is_trusted_external_artwork_url(
      "https://image-cdn-ak.spotifycdn.com/image/ab67706c0000da84513ab59cccfcf9bef6cabe2f"));
  CHECK(is_trusted_external_artwork_url(
      "https://spotify-static.ws.sonos.com/icons/playlist_folder_legacy.png"));
  const std::string official_roam_photo =
      official_sonos_product_image_url("Sonos Roam", "S27");
  CHECK(!official_roam_photo.empty());
  CHECK(is_trusted_external_artwork_url(official_roam_photo));
  CHECK(official_sonos_product_image_url("Unknown", "S27") ==
        official_roam_photo);
  CHECK(official_sonos_product_image_url("Unknown", "S999").empty());
  CHECK(!is_trusted_external_artwork_url(
      official_roam_photo + "&untrusted=1"));
  CHECK(is_trusted_external_artwork_url(
      "https://sali.sonos.radio/image?w=60&image=https%3A%2F%2Fcdn-radiotime-logos.tunein.com%2Fs9483g.png&partnerId=tunein"));
  CHECK(is_trusted_external_artwork_url(
      "https://sali.sonos.superhi.fi/image?w=60&image=https%3A%2F%2Fcdn-profiles.tunein.com%2Fs6707%2Fimages%2Flogog.png%3Ft%3D160201&partnerId=tunein"));
  CHECK(is_trusted_external_artwork_url(
      "https://d1uner0r1fcap8.cloudfront.net/image?w=60&image=https%3A%2F%2Fcdn-profiles.tunein.com%2Fs6707%2Fimages%2Flogoq.png%3Ft%3D636268&partnerId=tunein"));
  CHECK(!is_trusted_external_artwork_url(
      "https://cdn-radiotime-logos.tunein.com/s9483g.png"));
  CHECK(!is_trusted_external_artwork_url(
      "https://sali.sonos.radio/image?w=60&image=https%3A%2F%2Fexample.com%2Flogo.png&partnerId=tunein"));
  CHECK(!is_trusted_external_artwork_url(
      "https://sali.sonos.radio/image?w=60&image=https%3A%2F%2Fcdn-radiotime-logos.tunein.com%2Fs9483g.png&partnerId=other"));
  CHECK(!is_trusted_external_artwork_url(
      "http://i.scdn.co/image/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  CHECK(!is_trusted_external_artwork_url(
      "https://i.scdn.co/image/0123456789abcdef0123456789abcdef0123456789abcdef"));
  CHECK(!is_trusted_external_artwork_url(
      "https://image-cdn-example.spotifycdn.com/image/ab67706c0000da84513ab59cccfcf9bef6cabe2f"));
  CHECK(resolve_url("http://192.168.1.42:1400/xml/device.xml", "/getaa?a=1") ==
        "http://192.168.1.42:1400/getaa?a=1");
  CHECK(resolve_url("http://192.168.1.42:1400/xml/device.xml", "service.xml") ==
        "http://192.168.1.42:1400/xml/service.xml");
  CHECK(valid_ipv4("192.168.1.1"));
  CHECK(!valid_ipv4("999.1.1.1"));

  std::string decoded;
  std::string error;
  CHECK(decode_chunked_body("4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n",
                            decoded, 32, error));
  CHECK(decoded == "Wikipedia");
  CHECK(!decode_chunked_body("z\r\nbroken\r\n0\r\n\r\n", decoded, 32, error));
  CHECK(!decode_chunked_body("20\r\nsmall\r\n", decoded, 8, error));
}

void test_xml_and_soap() {
  CHECK(xml_escape("<&\"'>") == "&lt;&amp;&quot;&apos;&gt;");
  std::string decoded;
  CHECK(xml_decode_entities("Rock &amp; Roll &#x266B;", decoded));
  CHECK(decoded.find("Rock & Roll") == 0);
  CHECK(!xml_decode_entities("&unknown;", decoded));
  auto xml = parse_xml(
      "<?xml version=\"1.0\"?><s:root xmlns:s=\"x\"><s:value a=\"1\">"
      "Hello &amp; goodbye</s:value></s:root>");
  CHECK(xml.ok);
  CHECK(xml_text(xml.root, "value") == "Hello & goodbye");
  CHECK(xml_attribute(*xml_find(xml.root, "value"), "a") == "1");
  auto deep = parse_xml(
      "<a><a><a><a><a><a><a><a><a></a></a></a></a></a></a></a></a></a>",
      1024, 100, 4);
  CHECK(!deep.ok);

  const auto envelope = make_soap_envelope(
      "urn:schemas-upnp-org:service:AVTransport:1", "SetAVTransportURI",
      {{"InstanceID", "0"}, {"CurrentURI", "x&y"}});
  CHECK(envelope.find("<CurrentURI>x&amp;y</CurrentURI>") != std::string::npos);
  CHECK(envelope.find("<u:SetAVTransportURI") != std::string::npos);
  const auto fault = parse_soap_fault(fixture("soap_fault.xml"));
  CHECK(fault.code == 711);
  CHECK(fault.description == "Illegal seek target");
}

void test_descriptions_and_topology() {
  auto parsed = parse_device_description(
      "http://192.168.1.42:1400/xml/device_description.xml",
      device_description());
  CHECK(parsed.ok());
  CHECK(parsed.value.room_name == "Living Room");
  CHECK(parsed.value.uuid == "RINCON_BAR");
  CHECK(parsed.value.device_description_url ==
        "http://192.168.1.42:1400/xml/device_description.xml");
  CHECK(parsed.value.services.count("AVTransport") == 1);
  CHECK(parsed.value.services["AVTransport"].control_url ==
        "http://192.168.1.42:1400/MediaRenderer/AVTransport/Control");

  Player surround;
  surround.uuid = "RINCON_SURROUND";
  Player sub;
  sub.uuid = "RINCON_SUB";
  auto topology =
      parse_topology(fixture("topology_bonded.xml"),
                     {parsed.value, surround, sub});
  CHECK(topology.ok());
  CHECK(topology.value.groups.size() == 1);
  CHECK(topology.value.groups[0].coordinator_uuid == "RINCON_BAR");
  CHECK(topology.value.groups[0].member_uuids.size() == 1);
  auto hidden = std::find_if(
      topology.value.players.begin(), topology.value.players.end(),
      [](const Player& player) { return player.uuid == "RINCON_SURROUND"; });
  CHECK(hidden != topology.value.players.end());
  CHECK(!hidden->visible);
  CHECK(hidden->bonded);
  auto hidden_sub = std::find_if(
      topology.value.players.begin(), topology.value.players.end(),
      [](const Player& player) { return player.uuid == "RINCON_SUB"; });
  CHECK(hidden_sub != topology.value.players.end());
  CHECK(!hidden_sub->visible);
  CHECK(hidden_sub->bonded);
}

void test_metadata_and_ranges() {
  CHECK(parse_transport_state("PLAYING") == TransportState::Playing);
  CHECK(parse_transport_state("PAUSED_PLAYBACK") == TransportState::Paused);
  CHECK(parse_transport_state("NEW_FUTURE_STATE") == TransportState::Unknown);
  CHECK(clamp_volume(-999) == 0);
  CHECK(clamp_volume(101) == 100);
  CHECK(parse_duration("1:02:03") == 3723);
  CHECK(parse_duration("NOT_IMPLEMENTED") == 0);
  CHECK(parse_duration("0:99:00") == 0);
  CHECK(format_duration(3723) == "1:02:03");
  CHECK(seek_target(3, -10, 100) == 0);
  CHECK(seek_target(95, 30, 100) == 100);

  Track radio;
  radio.uri = "x-sonosapi-stream:4855?sid=303&flags=8224&sn=4";
  radio.source = "object.item.audioItem.audioBroadcast";
  CHECK(is_radio_stream(radio));
  CHECK(is_technical_media_text("object.item.audioItem.audioBroadcast"));
  CHECK(strip_radio_backend_suffix("NPO 3FM-BB-AAC") == "NPO 3FM");
  CHECK(strip_radio_backend_suffix("3FM-BB-ACC") == "3FM");
  CHECK(!is_technical_media_text("3FM-BB-AAC"));
  CHECK(!is_technical_media_text("NPO 3FM"));
  Track music;
  music.uri = "x-rincon-queue:RINCON_TEST#0";
  music.source = "object.item.audioItem.musicTrack";
  CHECK(!is_radio_stream(music));

  Track saved_playlist_metadata;
  saved_playlist_metadata.id = "SQ:42";
  saved_playlist_metadata.source = "object.container";
  CHECK(is_saved_playlist_container(saved_playlist_metadata));

  const std::string didl =
      R"(<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/" )"
      R"(xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">)"
      R"(<item id="1"><dc:title>Song &amp; Dance</dc:title>)"
      R"(<dc:creator>Artist</dc:creator><upnp:album>Album</upnp:album>)"
      R"(<upnp:albumArtURI>/getaa?u=x&amp;s=1</upnp:albumArtURI>)"
      R"(<res duration="0:03:20">x-rincon-queue:test</res></item></DIDL-Lite>)";
  const auto track = parse_didl_track(didl);
  CHECK(track.title == "Song & Dance");
  CHECK(track.artist == "Artist");
  CHECK(track.album == "Album");
  CHECK(track.duration_seconds == 200);
  Player player;
  player.base_url = "http://192.168.1.42:1400";
  CHECK(artwork_url(player, track.artwork_uri) ==
        "http://192.168.1.42:1400/getaa?u=x&s=1");
  const std::string spotify_artwork =
      "https://i.scdn.co/image/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  CHECK(artwork_url(player, spotify_artwork).empty());
  CHECK(artwork_url(player, spotify_artwork, true) == spotify_artwork);
  CHECK(artwork_url(player, "file:///etc/passwd").empty());

  const std::string browse_response =
      R"(<Envelope><Result>&lt;DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/"&gt;&lt;item id="FV:2/empty"&gt;&lt;dc:title&gt;Favorites&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.sonos-favorite&lt;/upnp:class&gt;&lt;/item&gt;&lt;item id="FV:2/radio"&gt;&lt;dc:title&gt;NPO 3FM&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.sonos-favorite&lt;/upnp:class&gt;&lt;res&gt;x-sonosapi-stream:4855&lt;/res&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;</Result><NumberReturned>2</NumberReturned><TotalMatches>2</TotalMatches></Envelope>)";
  const auto browse = parse_browse_response(browse_response);
  CHECK(browse.ok());
  CHECK(browse.value.number_returned == 2);
  CHECK(browse.value.total_matches == 2);
  CHECK(browse.value.items.size() == 1);
  CHECK(browse.value.items.front().title == "NPO 3FM");
}

void test_settings_and_cache() {
  const std::string directory = temp_directory();
  CHECK(!directory.empty());
  {
    std::ofstream input(fs::path(directory) / "settings.ini");
    input << "schema_version=99\nvolume_step=5\nseek_seconds=15\n"
             "auto_artwork=0\nspotify_https_artwork=0\n"
             "official_sonos_product_photos=0\n"
             "manual_ips=192.168.1.8,invalid\nbutton_left=next_track\n"
             "future_option=keep-me\n";
  }
  SettingsStore store(directory);
  std::string warning;
  Settings settings = store.load(&warning);
  CHECK(!warning.empty());
  CHECK(settings.volume_step == 5);
  CHECK(settings.seek_seconds == 15);
  CHECK(settings.manual_ips.size() == 1);
  CHECK(settings.button_mapping[button_index(PhysicalButton::Left)] ==
        Action::Next);
  CHECK(settings.auto_artwork);
  CHECK(settings.spotify_https_artwork);
  CHECK(settings.official_sonos_product_photos);
  CHECK(settings.idle_battery_saver);
  CHECK(settings.unknown_fields["future_option"] == "keep-me");
  settings.volume_step = 3;
  settings.spotify_https_artwork = true;
  settings.official_sonos_product_photos = true;
  settings.idle_battery_saver = false;
  CHECK(store.save(settings));
  Settings saved = store.load();
  CHECK(saved.volume_step == 3);
  CHECK(saved.spotify_https_artwork);
  CHECK(saved.official_sonos_product_photos);
  CHECK(!saved.idle_battery_saver);
  CHECK(saved.button_mapping[button_index(PhysicalButton::Left)] ==
        Action::Next);
  CHECK(saved.unknown_fields["future_option"] == "keep-me");
  CHECK(!fs::exists(fs::path(directory) / "settings.ini.tmp"));
  settings.auto_artwork = false;
  settings.spotify_https_artwork = false;
  settings.official_sonos_product_photos = false;
  CHECK(store.save(settings));
  const Settings owner_disabled_artwork = store.load();
  CHECK(!owner_disabled_artwork.auto_artwork);
  CHECK(!owner_disabled_artwork.spotify_https_artwork);
  CHECK(!owner_disabled_artwork.official_sonos_product_photos);
  Settings unsafe;
  unsafe.button_mapping.fill(Action::None);
  validate_settings(unsafe);
  CHECK(unsafe.button_mapping == kDefaultButtonMapping);
  CHECK(kDefaultButtonMapping[button_index(PhysicalButton::L1)] ==
        Action::SpeakerVolumes);
  CHECK(kDefaultButtonMapping[button_index(PhysicalButton::R1)] ==
        Action::NextGroup);
  CHECK(button_mapping_is_safe(saved.button_mapping));
  Settings previous_defaults;
  previous_defaults.button_mapping = kRefreshDefaultButtonMapping;
  CHECK(store.save(previous_defaults));
  CHECK(store.load().button_mapping == kDefaultButtonMapping);
  Settings latest_previous_defaults;
  latest_previous_defaults.button_mapping =
      kSpeakerVolumesPreviousDefaultButtonMapping;
  CHECK(store.save(latest_previous_defaults));
  CHECK(store.load().button_mapping == kDefaultButtonMapping);
  Settings group_switch_previous_defaults;
  group_switch_previous_defaults.button_mapping =
      kGroupSwitchPreviousDefaultButtonMapping;
  CHECK(store.save(group_switch_previous_defaults));
  CHECK(store.load().button_mapping == kDefaultButtonMapping);

  ArtworkCache cache((fs::path(directory) / "art").string(), 1400);
  std::string png(900, 'x');
  const unsigned char header[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
                                  0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                  0, 0, 0, 1, 0, 0, 0, 1};
  std::copy(std::begin(header), std::end(header), png.begin());
  CHECK(cache.store("http://192.168.1.1/a.png", png));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  CHECK(cache.store("http://192.168.1.1/b.png", png));
  CHECK(cache.size_bytes() <= 1400);
  CHECK(cache.key_for("same") == cache.key_for("same"));
  CHECK(cache.key_for("../escape").find('/') == std::string::npos);
  CHECK(cache.clear());
  CHECK(cache.size_bytes() == 0);
  fs::remove_all(directory);
}

void test_bounded_queue() {
  struct Item {
    int kind;
    int value;
  };
  BoundedQueue<Item> queue(2);
  CHECK(queue.try_push({1, 10}));
  CHECK(queue.try_push({2, 20}));
  CHECK(!queue.try_push({3, 30}));
  CHECK(queue.replace_latest([](const Item& item) { return item.kind == 2; },
                             {2, 99}));
  Item first{};
  Item second{};
  CHECK(queue.try_pop(first));
  CHECK(queue.try_pop(second));
  CHECK(first.value == 10);
  CHECK(second.value == 99);
}

void test_live_mock_if_requested() {
  const char* ip = std::getenv("MIYONOS_INTEGRATION_IP");
  if (!ip) {
    std::cout << "Integration mock not requested; protocol unit tests only.\n";
    return;
  }
  SonosAdapter adapter;
  auto discovery = adapter.discover({ip}, 200);
  CHECK(discovery.ok());
  CHECK(!discovery.value.empty());
  if (!discovery.ok()) return;
  auto mock_player = std::find_if(
      discovery.value.begin(), discovery.value.end(),
      [ip](const Player& player) { return player.ip == ip; });
  CHECK(mock_player != discovery.value.end());
  if (mock_player == discovery.value.end()) return;
  auto topology = adapter.get_topology(*mock_player, discovery.value);
  CHECK(topology.ok());
  CHECK(!topology.value.groups.empty());
  auto playback = adapter.get_playback(*mock_player);
  CHECK(playback.ok());
  CHECK(playback.value.track.title == "A Local Song & Test");
  CHECK(playback.value.track.artwork_uri == "/getaa?s=1&u=mock");
  CHECK(playback.value.playlist_title == "Weekend Playlist");
  CHECK(playback.value.active_playlist_object_id == "SQ:1");
  CHECK(playback.value.transport_uri == "x-rincon-queue:RINCON_LIVING#0");
  CHECK(playback.value.volume == 28);
  CHECK(playback.value.track.seekable);
  auto speaker_volume = adapter.get_speaker_volume(*mock_player);
  CHECK(speaker_volume.ok());
  CHECK(speaker_volume.value.volume == 28);
  auto queue = adapter.browse(*mock_player, "Q:", 0, 60);
  CHECK(queue.ok());
  CHECK(queue.value.items.size() == 60);
  CHECK(queue.value.total_matches == 135);
  auto queue_page = adapter.browse(*mock_player, "Q:", 60, 60);
  CHECK(queue_page.ok());
  CHECK(queue_page.value.items.size() == 60);
  CHECK(queue_page.value.items.front().title == "Mock Track 61");
  auto current_playlist = adapter.browse(*mock_player, "SQ:1", 0, 60);
  CHECK(current_playlist.ok());
  CHECK(current_playlist.value.items.size() == 8);
  CHECK(current_playlist.value.total_matches == 8);
  auto saved_playlists = adapter.browse(*mock_player, "SQ:", 0, 60);
  CHECK(saved_playlists.ok());
  CHECK(saved_playlists.value.items.size() == 2);
  CHECK(saved_playlists.value.items.front().title == "Weekend Playlist");
  CHECK(saved_playlists.value.items.front().artwork_uri ==
        "/getaa?s=1&u=mock-1");
  CHECK(adapter.play_saved_playlist(*mock_player,
                                    saved_playlists.value.items.back())
            .ok());
  auto replaced_queue = adapter.browse(*mock_player, "Q:", 0, 60);
  CHECK(replaced_queue.ok());
  CHECK(replaced_queue.value.total_matches == 8);
  CHECK(replaced_queue.value.items.front().title == "Road Trip Track 1");
  auto favorites = adapter.browse(*mock_player, "FV:2", 0, 60);
  CHECK(favorites.ok());
  CHECK(favorites.value.items.size() == 3);
  CHECK(favorites.value.items.back().artwork_uri == "/getaa?s=1&u=mock");
  CHECK(is_playlist_favorite(favorites.value.items.back()));
  CHECK(adapter.play_item(*mock_player, favorites.value.items.back(), true).ok());
  auto favorite_replaced_queue = adapter.browse(*mock_player, "Q:0", 0, 60);
  CHECK(favorite_replaced_queue.ok());
  CHECK(favorite_replaced_queue.value.total_matches == 8);
  CHECK(favorite_replaced_queue.value.items.front().title == "Road Trip Track 1");
  BrowseItem container_favorite;
  container_favorite.playable = true;
  container_favorite.uri = "x-rincon-cpcontainer:1006206cfavorite";
  container_favorite.metadata =
      "<DIDL-Lite><item><dc:title>Favorite</dc:title></item></DIDL-Lite>";
  CHECK(adapter.play_item(*mock_player, container_favorite).ok());
  BrowseItem stream_favorite;
  stream_favorite.playable = true;
  stream_favorite.uri = "x-sonosapi-stream:transitioning";
  stream_favorite.metadata = container_favorite.metadata;
  CHECK(adapter.play_item(*mock_player, stream_favorite).ok());
  CHECK(adapter.set_volume(*mock_player, 33, true).ok());
  CHECK(adapter.adjust_group_volume(*mock_player, 2).ok());
  const auto adjusted_group_member = adapter.get_speaker_volume(*mock_player);
  CHECK(adjusted_group_member.ok());
  CHECK(adjusted_group_member.value.volume == 35);
  CHECK(adapter.set_mute(*mock_player, true, true).ok());
  CHECK(adapter.pause(*mock_player).ok());
  CHECK(adapter.stop(*mock_player).ok());
  CHECK(adapter.play(*mock_player).ok());
  CHECK(adapter.seek_time(*mock_player, 60).ok());

  HttpClient client;
  HttpClient::Limits limits;
  limits.connect_timeout_ms = 100;
  limits.read_timeout_ms = 100;
  limits.max_body_bytes = 1024;
  auto delayed = client.get("http://127.0.0.1:1400/delay", limits);
  CHECK(!delayed.ok());
  CHECK(delayed.error.find("timed out") != std::string::npos);
  auto refused = client.get("http://127.0.0.1:1/unavailable", limits);
  CHECK(!refused.ok());
  auto missing_art = client.get("http://127.0.0.1:1400/missing-art", limits);
  CHECK(missing_art.status == 404);

  const std::string directory = temp_directory();
  setenv("MIYONOS_PLAYER_IP", ip, 1);
  Controller controller(directory);
  controller.start();
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(7);
  while (std::chrono::steady_clock::now() < deadline &&
         controller.view().screen != Screen::NowPlaying) {
    controller.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(controller.view().screen == Screen::NowPlaying);
  controller.handle(Action::Controls);
  CHECK(controller.view().controls_overlay);
  controller.handle(Action::Back);
  CHECK(!controller.view().controls_overlay);
  const Screen menu_screens[] = {Screen::Rooms, Screen::Speakers,
                                 Screen::Queue, Screen::Favorites,
                                 Screen::Settings, Screen::Help,
                                 Screen::About, Screen::Diagnostics};
  for (int i = 0; i < 8; ++i) {
    controller.handle(Action::Menu);
    CHECK(controller.view().screen == Screen::Menu);
    controller.handle(Action::Previous);
    for (int row = 0; row < i; ++row) controller.handle(Action::Down);
    controller.handle(Action::Confirm);
    CHECK(controller.view().screen == menu_screens[i]);
    if (menu_screens[i] == Screen::Speakers) {
      const auto speakers_deadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(3);
      while (std::chrono::steady_clock::now() < speakers_deadline &&
             controller.view().speaker_volumes.size() < 2) {
        controller.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      CHECK(controller.view().speaker_volumes.size() >= 2);
      controller.handle(Action::Right);
      CHECK(controller.view().selection == 1);
      controller.handle(Action::Down);
      CHECK(controller.view().speaker_volume >= 0);
      const bool muted_before = controller.view().speaker_muted;
      controller.handle(Action::Confirm);
      CHECK(controller.view().speaker_muted != muted_before);
      const int selected_volume = controller.view().speaker_volume;
      controller.handle(Action::Context);
      CHECK(controller.view().toast.find("Syncing ") == 0);
      for (const auto& member :
           controller.view().topology.groups.front().member_uuids) {
        const auto volume = controller.view().speaker_volumes.find(member);
        CHECK(volume != controller.view().speaker_volumes.end());
        CHECK(volume->second.volume == selected_volume);
      }
      const auto sync_deadline = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(3);
      while (std::chrono::steady_clock::now() < sync_deadline &&
             controller.view().toast != "All speaker volumes synced") {
        controller.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      CHECK(controller.view().toast == "All speaker volumes synced");
      controller.handle(Action::Left);
      CHECK(controller.view().selection == 0);
    }
    controller.handle(Action::Back);
    CHECK(controller.view().screen == Screen::Menu);
    controller.handle(Action::Back);
    CHECK(controller.view().screen == Screen::NowPlaying);
  }
  controller.handle(Action::Rooms);
  controller.handle(Action::Context);
  CHECK(controller.view().screen == Screen::GroupEditor);
  controller.handle(Action::Back);
  controller.handle(Action::Back);
  controller.handle(Action::Menu);
  controller.handle(Action::Previous);
  for (int row = 0; row < 4; ++row) controller.handle(Action::Down);
  controller.handle(Action::Confirm);
  controller.handle(Action::Previous);
  for (int row = 0; row < 11; ++row) controller.handle(Action::Down);
  controller.handle(Action::Confirm);
  CHECK(controller.view().screen == Screen::IpEditor);
  controller.handle(Action::Back);
  controller.handle(Action::Previous);
  for (int row = 0; row < 14; ++row) controller.handle(Action::Down);
  controller.handle(Action::Confirm);
  CHECK(controller.view().screen == Screen::ButtonMapping);
  controller.handle(Action::Confirm);
  controller.handle(Action::Back);
  CHECK(controller.view().screen == Screen::ButtonMapping);
  controller.handle(Action::Context);
  controller.handle(Action::Back);
  CHECK(controller.view().screen == Screen::Settings);
  controller.handle(Action::Down);
  controller.handle(Action::Confirm);
  CHECK(controller.view().screen == Screen::ConfirmAction);
  controller.handle(Action::Back);
  controller.handle(Action::Back);
  controller.handle(Action::Back);
  CHECK(controller.view().screen == Screen::NowPlaying);
  controller.handle(Action::Queue);
  CHECK(controller.view().screen == Screen::Queue);
  controller.handle(Action::Context);
  CHECK(controller.view().screen == Screen::Playlists);
  const auto playlist_deadline = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < playlist_deadline &&
         controller.view().playlists.empty()) {
    controller.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(!controller.view().playlists.empty());
  const std::string selected_playlist =
      controller.view().playlists[controller.view().selection].title;
  controller.handle(Action::Confirm);
  CHECK(controller.view().screen == Screen::NowPlaying);
  CHECK(controller.view().playback.playlist_title == selected_playlist);
  const auto playback_deadline = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < playback_deadline &&
         controller.view().playback.transport_uri.rfind("x-rincon-queue:",
                                                         0) != 0) {
    controller.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(controller.view().playback.transport_uri.rfind("x-rincon-queue:",
                                                        0) == 0);
  CHECK(controller.view().playback.playlist_title == selected_playlist);
  const auto playlist_artwork_deadline = std::chrono::steady_clock::now() +
                                         std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < playlist_artwork_deadline &&
         controller.view().now_playing_playlist_artwork_path.empty()) {
    controller.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(controller.view().now_playing_playlist_artwork_title ==
        selected_playlist);
  CHECK(!controller.view().now_playing_playlist_artwork_path.empty());
  const auto context_deadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < context_deadline &&
         controller.settings().playlist_context_queue_fingerprint.empty()) {
    controller.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(!controller.settings().playlist_context_queue_fingerprint.empty());
  // Real playlist Favorites become a generic Sonos queue and do not keep a
  // playlist title in their later metadata polls. Re-selecting the same group
  // models the periodic topology refresh that must preserve Miyonos' context.
  controller.handle(Action::Rooms);
  CHECK(controller.view().screen == Screen::Rooms);
  controller.handle(Action::Confirm);
  CHECK(controller.view().screen == Screen::NowPlaying);
  const auto retained_playlist_deadline = std::chrono::steady_clock::now() +
                                          std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < retained_playlist_deadline &&
         controller.view().playback.playlist_title != selected_playlist) {
    controller.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(controller.view().playback.playlist_title == selected_playlist);
  CHECK(controller.view().now_playing_playlist_artwork_title ==
        selected_playlist);
  CHECK(!controller.view().now_playing_playlist_artwork_path.empty());
  controller.handle(Action::Queue);
  const auto queue_deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < queue_deadline &&
         controller.view().queue.empty()) {
    controller.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(controller.view().screen == Screen::Queue);
  CHECK(!controller.view().queue.empty());
  CHECK(controller.view().queue.front().title == "Road Trip Track 1");
  const auto queue_artwork_deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < queue_artwork_deadline &&
         (controller.view().queue_artwork_paths.empty() ||
          controller.view().queue_artwork_paths.front().empty())) {
    controller.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(controller.view().queue_artwork_paths.size() ==
        controller.view().queue.size());
  CHECK(!controller.view().queue_artwork_paths.empty());
  CHECK(!controller.view().queue_artwork_paths.front().empty());
  controller.handle(Action::Context);
  CHECK(controller.view().screen == Screen::Playlists);
  controller.handle(Action::Context);
  CHECK(controller.view().screen == Screen::Queue);
  controller.handle(Action::Back);
  controller.handle(Action::SpeakerVolumes);
  CHECK(controller.view().screen == Screen::Speakers);
  controller.handle(Action::SpeakerVolumes);
  CHECK(controller.view().screen == Screen::Speakers);
  controller.handle(Action::Back);
  CHECK(controller.view().screen == Screen::NowPlaying);
  CHECK(controller.view().group_volume_target);
  const int group_volume_before = controller.view().speaker_volume;
  std::map<std::string, int> member_volumes_before;
  for (const auto& member :
       controller.view().topology.groups.front().member_uuids) {
    const auto volume = controller.view().speaker_volumes.find(member);
    CHECK(volume != controller.view().speaker_volumes.end());
    if (volume != controller.view().speaker_volumes.end()) {
      member_volumes_before[member] = volume->second.volume;
    }
  }
  controller.handle(Action::Up);
  const int group_volume_after_up =
      clamp_volume(group_volume_before + controller.settings().volume_step);
  const auto group_volume_up_deadline = std::chrono::steady_clock::now() +
                                        std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < group_volume_up_deadline) {
    controller.update();
    bool every_member_updated = true;
    for (const auto& before : member_volumes_before) {
      const auto current = controller.view().speaker_volumes.find(before.first);
      every_member_updated = every_member_updated &&
          current != controller.view().speaker_volumes.end() &&
          current->second.volume == clamp_volume(
              before.second + controller.settings().volume_step);
    }
    if (every_member_updated &&
        controller.view().speaker_volume == group_volume_after_up) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(controller.view().speaker_volume == group_volume_after_up);
  for (const auto& before : member_volumes_before) {
    const auto current = controller.view().speaker_volumes.find(before.first);
    CHECK(current != controller.view().speaker_volumes.end());
    if (current != controller.view().speaker_volumes.end()) {
      CHECK(current->second.volume ==
            clamp_volume(before.second + controller.settings().volume_step));
    }
  }
  controller.handle(Action::Down);
  const auto group_volume_down_deadline = std::chrono::steady_clock::now() +
                                          std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < group_volume_down_deadline) {
    controller.update();
    bool every_member_restored = true;
    for (const auto& before : member_volumes_before) {
      const auto current = controller.view().speaker_volumes.find(before.first);
      every_member_restored = every_member_restored &&
          current != controller.view().speaker_volumes.end() &&
          current->second.volume == before.second;
    }
    if (every_member_restored &&
        controller.view().speaker_volume == group_volume_before) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(controller.view().speaker_volume == group_volume_before);
  for (const auto& before : member_volumes_before) {
    const auto current = controller.view().speaker_volumes.find(before.first);
    CHECK(current != controller.view().speaker_volumes.end());
    if (current != controller.view().speaker_volumes.end()) {
      CHECK(current->second.volume == before.second);
    }
  }
  // Next Speaker remains available as an optional custom action, but it is no
  // longer the R1 default. Returning to Now Playing restores group control.
  controller.handle(Action::NextSpeaker);
  CHECK(!controller.view().group_volume_target);
  controller.handle(Action::Back);
  CHECK(controller.view().group_volume_target);
  controller.handle(Action::NextGroup);
  CHECK(controller.view().group_volume_target);
  CHECK(controller.view().toast == "Only one group is available.");
  controller.handle(Action::ExitButton);
  CHECK(controller.view().screen == Screen::ConfirmExit);
  controller.handle(Action::ExitButton);
  CHECK(controller.exit_requested());
  controller.handle(Action::Back);
  CHECK(controller.view().screen == Screen::NowPlaying);
  controller.stop();
  Controller restarted(directory);
  restarted.start();
  const auto restarted_deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < restarted_deadline &&
         restarted.view().playback.playlist_title != selected_playlist) {
    restarted.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(restarted.view().screen == Screen::NowPlaying);
  CHECK(restarted.view().playback.playlist_title == selected_playlist);
  CHECK(restarted.view().now_playing_playlist_artwork_title ==
        selected_playlist);
  restarted.stop();

  // A playlist started by another Sonos controller has authoritative metadata.
  // It must replace, rather than inherit, the generic-queue fallback that was
  // persisted for the earlier Miyonos-started playlist.
  CHECK(adapter.play_saved_playlist(*mock_player,
                                    saved_playlists.value.items.front())
            .ok());
  Controller externally_changed(directory);
  externally_changed.start();
  const auto changed_deadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < changed_deadline &&
         externally_changed.view().playback.playlist_title !=
             saved_playlists.value.items.front().title) {
    externally_changed.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(externally_changed.view().playback.playlist_title ==
        saved_playlists.value.items.front().title);
  CHECK(externally_changed.settings().playlist_context_queue_fingerprint.empty());
  externally_changed.stop();
  fs::remove_all(directory);
}

void test_battery_gauge() {
  CHECK(battery_percent_from_gauge_register(0x00) == 0);
  CHECK(battery_percent_from_gauge_register(0x64) == 100);
  CHECK(battery_percent_from_gauge_register(0xe5) == -1);
}

}  // namespace

int main() {
  test_battery_gauge();
  test_ssdp();
  test_url_and_http_helpers();
  test_xml_and_soap();
  test_descriptions_and_topology();
  test_metadata_and_ranges();
  test_settings_and_cache();
  test_bounded_queue();
  test_live_mock_if_requested();
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
