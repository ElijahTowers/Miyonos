#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "network/http.h"
#include "simulator/mock_sonos.h"
#include "sonos/protocol.h"

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

struct Session {
  explicit Session(const std::string& scenario) : fixture(scenario) {
    CHECK(fixture.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  ~Session() {
    fixture.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  SimulatorSonosFixture fixture;
};

std::vector<Player> discover(SonosAdapter& adapter) {
  auto result = adapter.discover({"127.0.0.1"}, 100);
  CHECK(result.ok());
  CHECK(result.value.size() == 1);
  return result.value;
}

void test_normal_and_group_scenarios() {
  {
    Session session("normal");
    SonosAdapter adapter;
    const auto players = discover(adapter);
    if (players.empty()) return;
    auto topology = adapter.get_topology(players.front(), players);
    CHECK(topology.ok());
    CHECK(topology.value.groups.size() == 1);
    CHECK(topology.value.groups.front().member_uuids.size() == 1);
    auto playback = adapter.get_playback(players.front());
    CHECK(playback.ok());
    CHECK(playback.value.track.title == "A Local Song & Test");
    CHECK(playback.value.active_playlist_object_id == "SQ:1");
    CHECK(playback.value.volume == 28);
    const auto speaker_volume = adapter.get_speaker_volume(players.front());
    CHECK(speaker_volume.ok());
    CHECK(speaker_volume.value.volume == 28);
  }
  {
    Session session("multi-room");
    SonosAdapter adapter;
    const auto players = discover(adapter);
    if (players.empty()) return;
    auto topology = adapter.get_topology(players.front(), players);
    CHECK(topology.ok());
    CHECK(topology.value.groups.size() == 2);
  }
  {
    Session session("grouped");
    SonosAdapter adapter;
    const auto players = discover(adapter);
    if (players.empty()) return;
    auto topology = adapter.get_topology(players.front(), players);
    CHECK(topology.ok());
    CHECK(topology.value.groups.size() == 1);
    CHECK(topology.value.groups.front().member_uuids.size() == 2);
  }
}

void test_content_scenarios() {
  {
    Session session("grouped");
    HttpClient client;
    const auto artwork = client.get("http://127.0.0.1:1400/getaa?u=mock");
    CHECK(artwork.ok());
    CHECK(artwork.body.size() > 24);
    CHECK(static_cast<unsigned char>(artwork.body[0]) == 0x89);
    CHECK(artwork.body.compare(1, 3, "PNG") == 0);
  }
  {
    Session session("long-queue");
    SonosAdapter adapter;
    const auto players = discover(adapter);
    if (players.empty()) return;
    auto first = adapter.browse(players.front(), "Q:", 0, 60);
    auto last = adapter.browse(players.front(), "Q:", 300, 60);
    CHECK(first.ok());
    CHECK(last.ok());
    CHECK(first.value.items.size() == 60);
    CHECK(last.value.items.size() == 60);
    CHECK(first.value.total_matches == 360);
    CHECK(last.value.items.back().title == "Mock Track 360");
    auto current_playlist = adapter.browse(players.front(), "SQ:1", 0, 60);
    CHECK(current_playlist.ok());
    CHECK(current_playlist.value.items.size() == 8);
    CHECK(current_playlist.value.total_matches == 8);
    CHECK(current_playlist.value.items.front().title == "Mock Track 1");
    auto saved_playlists = adapter.browse(players.front(), "SQ:", 0, 60);
    CHECK(saved_playlists.ok());
    CHECK(saved_playlists.value.items.size() == 2);
    CHECK(saved_playlists.value.items.back().title == "Road Trip Playlist");
    CHECK(saved_playlists.value.items.back().artwork_uri ==
          "/getaa?s=1&u=mock-2");
    CHECK(adapter.play_saved_playlist(players.front(),
                                      saved_playlists.value.items.back())
              .ok());
    auto replaced_queue = adapter.browse(players.front(), "Q:", 0, 60);
    CHECK(replaced_queue.ok());
    CHECK(replaced_queue.value.total_matches == 8);
    CHECK(replaced_queue.value.items.front().title == "Road Trip Track 1");
    auto favorites = adapter.browse(players.front(), "FV:2", 0, 60);
    CHECK(favorites.ok());
    CHECK(favorites.value.items.size() == 3);
    CHECK(favorites.value.items.back().artwork_uri == "/getaa?s=1&u=mock");
    CHECK(is_playlist_favorite(favorites.value.items.back()));
    CHECK(adapter.play_item(players.front(), favorites.value.items.back(), true)
              .ok());
    auto favorite_replaced_queue = adapter.browse(players.front(), "Q:0", 0, 60);
    CHECK(favorite_replaced_queue.ok());
    CHECK(favorite_replaced_queue.value.total_matches == 8);
    CHECK(favorite_replaced_queue.value.items.front().title ==
          "Road Trip Track 1");
  }
  {
    Session session("mixed-favorites");
    SonosAdapter adapter;
    const auto players = discover(adapter);
    if (players.empty()) return;
    const auto first = adapter.browse(players.front(), "FV:2", 0, 60);
    const auto second = adapter.browse(players.front(), "FV:2", 60, 60);
    CHECK(first.ok());
    CHECK(second.ok());
    CHECK(first.value.total_matches == 180);
    CHECK(first.value.items.size() == 60);
    CHECK(second.value.items.size() == 60);
    CHECK(is_playlist_favorite(first.value.items[3]));
    CHECK(is_playlist_favorite(second.value.items[3]));
  }
  {
    // A My Songs-style favorite can contain thousands of tracks. The fixture
    // rejects AddURIToQueue for it, so this only succeeds when Miyonos opens
    // the cpcontainer directly instead of asking Sonos to expand it at once.
    Session session("large-collection");
    SonosAdapter adapter;
    const auto players = discover(adapter);
    if (players.empty()) return;
    const auto favorites = adapter.browse(players.front(), "FV:2", 0, 60);
    CHECK(favorites.ok());
    CHECK(favorites.value.items.size() == 1);
    CHECK(!is_playlist_favorite(favorites.value.items.front()));
    CHECK(favorites.value.items.front().title == "My Songs");
    CHECK(adapter.play_item(players.front(), favorites.value.items.front(), false)
              .ok());
  }
  {
    // A real music service can take longer than the normal 2.5-second SOAP
    // deadline while the speaker opens a large direct collection. This
    // fixture proves that only this bounded start path waits longer; it still
    // must not attempt to enqueue the collection.
    Session session("large-collection-slow");
    SonosAdapter adapter;
    const auto players = discover(adapter);
    if (players.empty()) return;
    const auto favorites = adapter.browse(players.front(), "FV:2", 0, 60);
    CHECK(favorites.ok());
    CHECK(favorites.value.items.size() == 1);
    const auto started = std::chrono::steady_clock::now();
    CHECK(adapter.play_item(players.front(), favorites.value.items.front(), false)
              .ok());
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    CHECK(elapsed.count() >= 2900);
    CHECK(elapsed.count() < 10000);
  }
  {
    Session session("no-artwork");
    HttpClient client;
    const auto missing = client.get("http://127.0.0.1:1400/getaa?u=mock");
    CHECK(missing.status == 404);
    CHECK(!missing.ok());
  }
}

void test_slow_and_coordinator_change() {
  {
    Session session("slow");
    SonosAdapter adapter;
    const auto players = discover(adapter);
    if (players.empty()) return;
    const auto started = std::chrono::steady_clock::now();
    auto playback = adapter.get_playback(players.front());
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    CHECK(playback.ok());
    CHECK(elapsed.count() >= 1500);
  }
  {
    Session session("coordinator-change");
    SonosAdapter adapter;
    const auto players = discover(adapter);
    if (players.empty()) return;
    auto first = adapter.get_topology(players.front(), players);
    auto second = adapter.get_topology(players.front(), players);
    CHECK(first.ok());
    CHECK(second.ok());
    CHECK(first.value.groups.front().coordinator_uuid == "RINCON_LIVING");
    CHECK(second.value.groups.front().coordinator_uuid == "RINCON_KITCHEN");
  }
}

void test_offline_after_cleanup() {
  HttpClient client;
  HttpClient::Limits limits;
  limits.connect_timeout_ms = 100;
  limits.read_timeout_ms = 100;
  const auto response =
      client.get("http://127.0.0.1:1400/__simulator__/health", limits);
  CHECK(!response.ok());
}

}  // namespace

int main() {
  setenv("MIYONOS_DISABLE_SSDP", "1", 1);
  test_normal_and_group_scenarios();
  test_content_scenarios();
  test_slow_and_coordinator_change();
  test_offline_after_cleanup();
  std::cout << checks << " simulator fixture checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
