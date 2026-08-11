#include "app/controller.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <iomanip>
#include <iterator>
#include <arpa/inet.h>
#include <net/if.h>
#include <sstream>

#include "network/http.h"
#include "network/https_artwork.h"
#include "platform/clock.h"
#include "platform/logger.h"

namespace fs = std::filesystem;

namespace miyonos {

namespace {

constexpr std::size_t kBrowsePageSize = 60;
constexpr uint64_t kIdleBatterySaverDelayMs = 60 * 1000;
constexpr int kIdlePlayingPollIntervalMs = 15000;
constexpr int kIdlePausedPollIntervalMs = 60000;
constexpr int kIdleTopologyIntervalMs = 120000;

uint64_t idle_battery_saver_delay_ms() {
#ifdef MIYONOS_ENABLE_SIMULATOR
  // The screenshot harness can exercise the 60-second device behavior without
  // making the full suite wait a minute. Target builds never read this value.
  if (const char* configured =
          std::getenv("MIYONOS_TEST_IDLE_SAVER_DELAY_MS")) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(configured, &end, 10);
    if (end && *end == '\0' && value > 0 && value <= 30000) {
      return static_cast<uint64_t>(value);
    }
  }
#endif
  return kIdleBatterySaverDelayMs;
}

std::string queue_fingerprint(const std::vector<BrowseItem>& items) {
  if (items.empty()) return {};
  std::uint64_t hash = 1469598103934665603ULL;
  const auto append = [&hash](const std::string& value) {
    for (const unsigned char character : value) {
      hash ^= character;
      hash *= 1099511628211ULL;
    }
    hash ^= 0xff;
    hash *= 1099511628211ULL;
  };
  const std::size_t count = std::min<std::size_t>(8, items.size());
  for (std::size_t index = 0; index < count; ++index) {
    append(items[index].id);
    append(items[index].title);
    append(items[index].artist);
    append(items[index].uri);
  }
  std::ostringstream result;
  result << std::hex << std::setw(16) << std::setfill('0') << hash;
  return result.str();
}

bool is_saved_playlist_object_id(const std::string& object_id) {
  return lowercase(object_id).rfind("sq:", 0) == 0;
}

bool is_queue_transport_uri(const std::string& uri) {
  return lowercase(uri).rfind("x-rincon-queue:", 0) == 0;
}

bool is_queue_playback(const PlaybackSnapshot& playback) {
  return is_queue_transport_uri(playback.transport_uri) ||
         is_queue_transport_uri(playback.track.uri);
}

std::string ip_from_octets(const std::array<int, 4>& octets) {
  return std::to_string(octets[0]) + "." + std::to_string(octets[1]) + "." +
         std::to_string(octets[2]) + "." + std::to_string(octets[3]);
}

std::string now_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto value = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &value);
#else
  localtime_r(&value, &tm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
  return buffer;
}

std::string local_ipv4() {
  ifaddrs* addresses = nullptr;
  if (getifaddrs(&addresses) != 0) return "Not detected";
  std::string result = "Not detected";
  for (ifaddrs* item = addresses; item; item = item->ifa_next) {
    if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
        (item->ifa_flags & IFF_LOOPBACK) != 0 ||
        (item->ifa_flags & IFF_UP) == 0) {
      continue;
    }
    char buffer[INET_ADDRSTRLEN] = {};
    const auto* address =
        reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
    if (inet_ntop(AF_INET, &address->sin_addr, buffer, sizeof(buffer))) {
      result = buffer;
      break;
    }
  }
  freeifaddrs(addresses);
  return result;
}

std::string snapshot_text(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
                return c < 0x20 || c == '=';
              }),
              value.end());
  if (value.size() > 96) value.resize(96);
  return value;
}

void persist_topology_snapshot(const std::string& data_directory,
                               const Topology& topology) {
  const fs::path destination =
      fs::path(data_directory) / "topology.snapshot";
  const fs::path temporary =
      fs::path(data_directory) / "topology.snapshot.tmp";
  std::error_code ec;
  fs::create_directories(data_directory, ec);
  std::ofstream output(temporary, std::ios::trunc);
  if (!output) return;
  output << "schema=1\n"
         << "app_version=" << MIYONOS_VERSION << "\n"
         << "room_count=" << topology.players.size() << "\n"
         << "group_count=" << topology.groups.size() << "\n";
  for (std::size_t i = 0; i < topology.players.size(); ++i) {
    output << "room." << i << ".name="
           << snapshot_text(topology.players[i].room_name) << "\n";
  }
  for (std::size_t i = 0; i < topology.groups.size(); ++i) {
    output << "group." << i << ".name="
           << snapshot_text(topology.groups[i].name) << "\n"
           << "group." << i << ".members="
           << topology.groups[i].member_uuids.size() << "\n";
  }
  output.close();
  if (!output) {
    fs::remove(temporary, ec);
    return;
  }
  fs::rename(temporary, destination, ec);
}

}  // namespace

Controller::Controller(std::string data_directory)
    : data_directory_(std::move(data_directory)),
      settings_store_(data_directory_),
      settings_(settings_store_.load()),
      artwork_cache_((fs::path(data_directory_) / "artwork").string(),
                     static_cast<std::size_t>(settings_.artwork_cache_mb) *
                         1024 * 1024) {
  if (const char* player_ip = std::getenv("MIYONOS_PLAYER_IP")) {
    if (valid_ipv4(player_ip) &&
        std::find(settings_.manual_ips.begin(), settings_.manual_ips.end(),
                  player_ip) == settings_.manual_ips.end()) {
      settings_.manual_ips.emplace_back(player_ip);
    }
  }
  view_.diagnostics.version = MIYONOS_VERSION;
  view_.last_input_ms = monotonic_ms();
}

Controller::~Controller() { stop(); }

void Controller::start() {
  if (worker_.joinable() || artwork_worker_.joinable()) return;
  cancelled_.store(false);
  Logger::instance().initialize((fs::path(data_directory_) / "logs").string(),
                                settings_.diagnostics_mode);
  MIYONOS_LOG("app", "Miyonos " MIYONOS_VERSION " starting");
  worker_ = std::thread(&Controller::worker_loop, this);
  artwork_worker_ = std::thread(&Controller::artwork_worker_loop, this);
  view_.screen = Screen::Splash;
  view_.status = "Checking Wi-Fi...";
  begin_discovery();
}

void Controller::stop() {
  if (!worker_.joinable() && !artwork_worker_.joinable()) return;
  cancelled_.store(true);
  Command stop_command;
  stop_command.type = CommandType::Stop;
  commands_.try_push(std::move(stop_command));
  Command artwork_stop_command;
  artwork_stop_command.type = CommandType::Stop;
  artwork_commands_.try_push(std::move(artwork_stop_command));
  commands_.close();
  artwork_commands_.close();
  if (worker_.joinable()) worker_.join();
  if (artwork_worker_.joinable()) artwork_worker_.join();
  results_.close();
  save_settings();
  MIYONOS_LOG("app", "Miyonos stopped cleanly");
}

bool Controller::enqueue(Command command) {
  if (command.type == CommandType::DownloadArtwork &&
      artwork_paused_.load()) {
    return false;
  }
  if (command.type == CommandType::DownloadArtwork ||
      command.type == CommandType::ClearCache) {
    if (!artwork_commands_.try_push(std::move(command))) {
      show_toast("Artwork is still loading.");
      return false;
    }
    return true;
  }
  if (command.type == CommandType::Volume) {
    const std::string player_uuid = command.player.uuid;
    const bool group_volume = command.flag;
    return commands_.replace_latest(
        [&player_uuid, group_volume](const Command& item) {
          return item.type == CommandType::Volume &&
                 item.player.uuid == player_uuid && item.flag == group_volume;
        },
        std::move(command));
  }
  if (!commands_.try_push(std::move(command))) {
    show_toast("Please wait for the current action.");
    return false;
  }
  return true;
}

void Controller::note_user_activity() {
  view_.last_input_ms = monotonic_ms();
  if (view_.idle_battery_saver_active) {
    view_.idle_battery_saver_active = false;
    artwork_paused_.store(false);
    MIYONOS_LOG("power", "Idle battery saver woke on input");
  }
}

void Controller::begin_discovery() {
  view_.discovering = true;
  view_.busy = true;
  view_.error.clear();
  view_.status = "Finding Sonos rooms...";
  Command command;
  command.type = CommandType::Discover;
  command.ips = settings_.cached_ips;
  command.ips.insert(command.ips.end(), settings_.manual_ips.begin(),
                     settings_.manual_ips.end());
  enqueue(std::move(command));
}

void Controller::request_topology() {
  if (view_.topology.players.empty()) return;
  Command command;
  command.type = CommandType::RefreshTopology;
  command.player = view_.topology.players.front();
  if (enqueue(std::move(command))) last_topology_requested_ms_ = monotonic_ms();
}

void Controller::request_poll() {
  const Player* selected = coordinator();
  if (!selected) return;
  Command command;
  command.type = CommandType::Poll;
  command.player = *selected;
  if (enqueue(std::move(command))) last_poll_requested_ms_ = monotonic_ms();
}

void Controller::request_speaker_volume() {
  if (view_.group_volume_target) {
    if (view_.playback.received_at_ms > 0) {
      view_.speaker_volume = view_.playback.volume;
      view_.speaker_muted = view_.playback.muted;
    } else {
      request_poll();
    }
    return;
  }
  const Player* target = volume_target();
  if (!target) return;
  Command command;
  command.type = CommandType::GetSpeakerVolume;
  command.player = *target;
  enqueue(std::move(command));
}

void Controller::request_group_speaker_volumes() {
  const Group* group = active_group();
  if (!group) return;
  for (const auto& uuid : group->member_uuids) {
    const Player* player = player_by_uuid(uuid);
    if (!player || !player->visible || !player->available) continue;
    Command command;
    command.type = CommandType::GetSpeakerVolume;
    command.player = *player;
    enqueue(std::move(command));
  }
}

void Controller::request_speaker_product_photos() {
  if (!settings_.official_sonos_product_photos) {
    view_.speaker_product_photo_paths.clear();
    speaker_product_photo_urls_.clear();
    failed_speaker_product_photo_urls_.clear();
    return;
  }
  if (artwork_paused_.load()) return;
  const Group* group = active_group();
  if (!group) return;

  std::set<std::string> active_speakers;
  for (const auto& uuid : group->member_uuids) {
    const Player* speaker = player_by_uuid(uuid);
    if (!speaker || !speaker->visible || !speaker->available) continue;
    active_speakers.insert(speaker->uuid);
    const std::string url = official_sonos_product_image_url(
        speaker->model_name, speaker->model_number);
    if (url.empty()) continue;
    if (speaker_product_photo_urls_[speaker->uuid] != url) {
      speaker_product_photo_urls_[speaker->uuid] = url;
      view_.speaker_product_photo_paths.erase(speaker->uuid);
    }
    const std::string cached = artwork_cache_.find(url);
    if (!cached.empty()) {
      view_.speaker_product_photo_paths[speaker->uuid] = cached;
      continue;
    }
    if (!speaker_product_photo_inflight_url_.empty() ||
        failed_speaker_product_photo_urls_.count(url) != 0) {
      continue;
    }
    Command command;
    command.type = CommandType::DownloadArtwork;
    command.text = url;
    command.flag = true;
    command.artwork_target = ArtworkTarget::SpeakerProduct;
    if (enqueue(std::move(command))) {
      speaker_product_photo_inflight_url_ = url;
    }
    return;
  }
  for (auto it = view_.speaker_product_photo_paths.begin();
       it != view_.speaker_product_photo_paths.end();) {
    if (active_speakers.count(it->first) == 0) {
      it = view_.speaker_product_photo_paths.erase(it);
    } else {
      ++it;
    }
  }
}

void Controller::request_selected_favorite_artwork() {
  if (!settings_.auto_artwork || view_.screen != Screen::Favorites ||
      view_.selection < 0 ||
      view_.selection >= static_cast<int>(view_.favorites.size())) {
    view_.favorite_artwork_path.clear();
    last_favorite_artwork_url_.clear();
    return;
  }
  if (artwork_paused_.load()) return;
  const Player* selected = coordinator();
  if (!selected) return;
  const BrowseItem& favorite = view_.favorites[view_.selection];
  const std::string url = artwork_url(*selected, favorite.artwork_uri,
                                      settings_.spotify_https_artwork);
  if (url.empty()) {
    view_.favorite_artwork_path.clear();
    last_favorite_artwork_url_.clear();
    return;
  }
  if (url == last_favorite_artwork_url_) return;
  last_favorite_artwork_url_ = url;
  view_.favorite_artwork_path.clear();
  const std::string cached = artwork_cache_.find(url);
  if (!cached.empty()) {
    view_.favorite_artwork_path = cached;
    return;
  }
  Command command;
  command.type = CommandType::DownloadArtwork;
  command.player = *selected;
  command.text = url;
  command.flag = settings_.spotify_https_artwork;
  command.artwork_target = ArtworkTarget::Favorite;
  enqueue(std::move(command));
}

void Controller::request_selected_playlist_artwork() {
  if (!settings_.auto_artwork || view_.screen != Screen::Playlists ||
      view_.selection < 0 ||
      view_.selection >= static_cast<int>(view_.playlists.size())) {
    view_.playlist_artwork_path.clear();
    last_playlist_artwork_url_.clear();
    return;
  }
  if (artwork_paused_.load()) return;
  const Player* selected = coordinator();
  if (!selected) return;
  const BrowseItem& playlist = view_.playlists[view_.selection];
  const std::string url = artwork_url(*selected, playlist.artwork_uri,
                                      settings_.spotify_https_artwork);
  if (url.empty()) {
    view_.playlist_artwork_path.clear();
    last_playlist_artwork_url_.clear();
    return;
  }
  if (url == last_playlist_artwork_url_) return;
  last_playlist_artwork_url_ = url;
  view_.playlist_artwork_path.clear();
  const std::string cached = artwork_cache_.find(url);
  if (!cached.empty()) {
    view_.playlist_artwork_path = cached;
    return;
  }
  Command command;
  command.type = CommandType::DownloadArtwork;
  command.player = *selected;
  command.text = url;
  command.flag = settings_.spotify_https_artwork;
  command.artwork_target = ArtworkTarget::Playlist;
  enqueue(std::move(command));
}

void Controller::request_now_playing_playlist_artwork(
    const BrowseItem& playlist) {
  view_.now_playing_playlist_artwork_title = playlist.title;
  if (!settings_.auto_artwork) {
    view_.now_playing_playlist_artwork_path.clear();
    last_now_playing_playlist_artwork_url_.clear();
    return;
  }
  if (artwork_paused_.load()) return;
  const Player* selected = coordinator();
  if (!selected) return;
  const std::string url = artwork_url(*selected, playlist.artwork_uri,
                                      settings_.spotify_https_artwork);
  if (url.empty()) {
    view_.now_playing_playlist_artwork_path.clear();
    last_now_playing_playlist_artwork_url_.clear();
    return;
  }
  if (url == last_now_playing_playlist_artwork_url_) return;
  last_now_playing_playlist_artwork_url_ = url;
  view_.now_playing_playlist_artwork_path.clear();

  // The playlist browser may already be downloading this exact cover. Reuse
  // its result instead of adding a duplicate request to the worker queue.
  if (url == last_playlist_artwork_url_) {
    view_.now_playing_playlist_artwork_path = view_.playlist_artwork_path;
    return;
  }
  const std::string cached = artwork_cache_.find(url);
  if (!cached.empty()) {
    view_.now_playing_playlist_artwork_path = cached;
    return;
  }
  Command command;
  command.type = CommandType::DownloadArtwork;
  command.player = *selected;
  command.text = url;
  command.flag = settings_.spotify_https_artwork;
  command.artwork_target = ArtworkTarget::NowPlayingPlaylist;
  enqueue(std::move(command));
}

void Controller::request_queue_artwork() {
  if (!settings_.auto_artwork || view_.screen != Screen::Queue ||
      view_.queue.empty()) {
    return;
  }
  if (artwork_paused_.load()) return;
  const Player* selected = coordinator();
  if (!selected) return;

  // Queue cover art is intentionally lazy: only the six visible rows plus one
  // row above and below them may be requested, and only one uncached cover is
  // ever in flight. This keeps playback controls responsive on the Miyoo and
  // avoids turning a long queue into a burst of network requests.
  if (view_.queue_artwork_paths.size() != view_.queue.size()) {
    view_.queue_artwork_paths.assign(view_.queue.size(), {});
  }
  if (queue_artwork_urls_.size() != view_.queue.size()) {
    queue_artwork_urls_.assign(view_.queue.size(), {});
  }
  for (std::size_t index = 0; index < view_.queue.size(); ++index) {
    const std::string url = artwork_url(*selected,
                                        view_.queue[index].artwork_uri,
                                        settings_.spotify_https_artwork);
    if (url != queue_artwork_urls_[index]) {
      queue_artwork_urls_[index] = url;
      view_.queue_artwork_paths[index].clear();
    }
  }

  constexpr int kVisibleRows = 6;
  const int count = static_cast<int>(view_.queue.size());
  int start = std::max(0, view_.selection - kVisibleRows / 2);
  start = std::min(start, std::max(0, count - kVisibleRows));
  const int first = std::max(0, start - 1);
  const int last = std::min(count, start + kVisibleRows + 1);

  for (int index = first; index < last; ++index) {
    const std::string& url = queue_artwork_urls_[index];
    if (url.empty() || failed_queue_artwork_urls_.count(url) != 0) continue;
    const std::string cached = artwork_cache_.find(url);
    if (!cached.empty()) {
      view_.queue_artwork_paths[index] = cached;
      continue;
    }
    if (!queue_artwork_inflight_url_.empty()) continue;
    Command command;
    command.type = CommandType::DownloadArtwork;
    command.player = *selected;
    command.text = url;
    command.flag = settings_.spotify_https_artwork;
    command.artwork_target = ArtworkTarget::Queue;
    if (enqueue(std::move(command))) {
      queue_artwork_inflight_url_ = url;
    }
    return;
  }
}

void Controller::request_browse(ListKind kind, const std::string& object_id,
                                std::size_t start_index, BrowseIntent intent) {
  const Player* selected = coordinator();
  if (!selected) {
    show_toast("Select an available room first.");
    return;
  }
  Command command;
  command.type = CommandType::Browse;
  command.player = *selected;
  command.list_kind = kind;
  command.browse_intent = intent;
  command.text = object_id.empty()
                     ? (kind == ListKind::Queue ? "Q:0"
                                                 : kind == ListKind::Favorites
                                                       ? "FV:2"
                                                       : "FV:2")
                     : object_id;
  command.index = start_index;
  command.visible_offset = kind == ListKind::Queue
                               ? view_.queue.size()
                               : kind == ListKind::Favorites
                                     ? view_.favorites.size()
                                     : view_.playlists.size();
  if (start_index == 0 && intent == BrowseIntent::Display) {
    if (kind == ListKind::Queue) queue_object_ = command.text;
    else if (kind == ListKind::Favorites) favorites_object_ = command.text;
    else {
      playlists_object_ = command.text;
      playlists_next_raw_index_ = 0;
      playlists_has_more_ = false;
    }
  }
  if (intent == BrowseIntent::Display) {
    view_.busy = true;
    view_.error.clear();
    view_.status = kind == ListKind::Queue
                       ? "Loading queue..."
                       : kind == ListKind::Favorites
                             ? "Loading favorites..."
                             : "Loading favorite playlists...";
  }
  if (!enqueue(std::move(command)) && intent == BrowseIntent::Display) {
    view_.busy = false;
  }
}

void Controller::capture_playlist_context() {
  if (selected_playlist_title_.empty() || !active_group()) {
    return;
  }
  request_browse(ListKind::Queue, "Q:0", 0,
                 BrowseIntent::CapturePlaylistContext);
}

void Controller::validate_playlist_context() {
  if (playlist_context_validation_pending_ ||
      settings_.playlist_context_group_id != view_.active_group_id ||
      settings_.playlist_context_title.empty() ||
      settings_.playlist_context_queue_fingerprint.empty()) {
    return;
  }
  const uint64_t now = monotonic_ms();
  if (now - last_playlist_context_validation_ms_ < 15000) return;
  last_playlist_context_validation_ms_ = now;
  playlist_context_validation_pending_ = true;
  request_browse(ListKind::Queue, "Q:0", 0,
                 BrowseIntent::ValidatePlaylistContext);
}

void Controller::restore_playlist_context_artwork() {
  view_.now_playing_playlist_artwork_title = selected_playlist_title_;
  if (!settings_.auto_artwork || selected_playlist_artwork_uri_.empty()) return;
  if (artwork_paused_.load()) return;
  const Player* selected = coordinator();
  if (!selected) return;
  const std::string url = artwork_url(*selected, selected_playlist_artwork_uri_,
                                      settings_.spotify_https_artwork);
  if (url.empty()) return;
  last_now_playing_playlist_artwork_url_ = url;
  const std::string cached = artwork_cache_.find(url);
  if (!cached.empty()) {
    view_.now_playing_playlist_artwork_path = cached;
    return;
  }
  Command command;
  command.type = CommandType::DownloadArtwork;
  command.player = *selected;
  command.text = url;
  command.flag = settings_.spotify_https_artwork;
  command.artwork_target = ArtworkTarget::NowPlayingPlaylist;
  enqueue(std::move(command));
}

void Controller::clear_playlist_context() {
  settings_.playlist_context_group_id.clear();
  settings_.playlist_context_title.clear();
  settings_.playlist_context_object_id.clear();
  settings_.playlist_context_artwork_uri.clear();
  settings_.playlist_context_queue_fingerprint.clear();
  save_settings();
}

Player* Controller::player_by_uuid(const std::string& uuid) {
  for (auto& player : view_.topology.players) {
    if (player.uuid == uuid) return &player;
  }
  return nullptr;
}

const Player* Controller::player_by_uuid(const std::string& uuid) const {
  for (const auto& player : view_.topology.players) {
    if (player.uuid == uuid) return &player;
  }
  return nullptr;
}

Group* Controller::active_group() {
  for (auto& group : view_.topology.groups) {
    if (group.id == view_.active_group_id) return &group;
  }
  return nullptr;
}

const Group* Controller::active_group() const {
  for (const auto& group : view_.topology.groups) {
    if (group.id == view_.active_group_id) return &group;
  }
  return nullptr;
}

const Player* Controller::coordinator() const {
  const Group* group = active_group();
  return group ? player_by_uuid(group->coordinator_uuid) : nullptr;
}

const Player* Controller::volume_target() const {
  const Group* group = active_group();
  if (!group) return nullptr;
  if (view_.group_volume_target) {
    const Player* selected = player_by_uuid(group->coordinator_uuid);
    return selected && selected->visible && selected->available ? selected
                                                                  : nullptr;
  }
  const bool active_room_is_member =
      std::find(group->member_uuids.begin(), group->member_uuids.end(),
                view_.active_room_uuid) != group->member_uuids.end();
  const Player* target =
      active_room_is_member ? player_by_uuid(view_.active_room_uuid) : nullptr;
  if (target && target->visible && target->available) return target;
  const Player* selected = player_by_uuid(group->coordinator_uuid);
  return selected && selected->visible && selected->available ? selected
                                                                : nullptr;
}

void Controller::cycle_volume_target(int direction) {
  const Group* group = active_group();
  if (!group) return;
  std::vector<const Player*> targets;
  for (const auto& uuid : group->member_uuids) {
    const Player* player = player_by_uuid(uuid);
    if (player && player->visible && player->available) targets.push_back(player);
  }
  if (targets.empty()) return;

  const int count = static_cast<int>(targets.size()) + 1;
  int index = 0;
  if (!view_.group_volume_target) {
    for (std::size_t item = 0; item < targets.size(); ++item) {
      if (targets[item]->uuid == view_.active_room_uuid) {
        index = static_cast<int>(item) + 1;
        break;
      }
    }
  }
  index = (index + direction % count + count) % count;
  if (index == 0) {
    view_.group_volume_target = true;
    view_.speaker_volume = view_.playback.volume;
    view_.speaker_muted = view_.playback.muted;
    request_poll();
    show_toast("Volume target: Group", 1800);
    return;
  }
  const Player* target = targets[static_cast<std::size_t>(index - 1)];
  view_.group_volume_target = false;
  view_.active_room_uuid = target->uuid;
  settings_.last_room_uuid = target->uuid;
  const auto cached = speaker_volumes_.find(target->uuid);
  view_.speaker_volume =
      cached == speaker_volumes_.end() ? -1 : cached->second.volume;
  view_.speaker_muted =
      cached == speaker_volumes_.end() ? false : cached->second.muted;
  save_settings();
  request_speaker_volume();
  show_toast("Volume target: " + target->room_name, 1800);
}

void Controller::cycle_group(int direction) {
  const std::size_t count = view_.topology.groups.size();
  if (count == 0) return;
  if (count == 1) {
    show_toast("Only one group is available.");
    return;
  }
  std::size_t current = 0;
  for (; current < count; ++current) {
    if (view_.topology.groups[current].id == view_.active_group_id) break;
  }
  if (current == count) current = 0;
  const int group_count = static_cast<int>(count);
  const int next =
      (static_cast<int>(current) + direction % group_count + group_count) %
      group_count;
  select_group(static_cast<std::size_t>(next));
  const Group& selected = view_.topology.groups[static_cast<std::size_t>(next)];
  show_toast("Group: " + selected.name);
}

void Controller::focus_speaker_card() {
  const Group* group = active_group();
  if (!group) return;
  std::vector<const Player*> speakers;
  for (const auto& uuid : group->member_uuids) {
    const Player* player = player_by_uuid(uuid);
    if (player && player->visible && player->available) speakers.push_back(player);
  }
  if (speakers.empty()) return;
  view_.selection = std::max(
      0, std::min<int>(view_.selection, static_cast<int>(speakers.size() - 1)));
  const Player* speaker = speakers[static_cast<std::size_t>(view_.selection)];
  // The dedicated Speaker Volumes screen is the sole default route to
  // individual room controls.
  view_.group_volume_target = false;
  view_.active_room_uuid = speaker->uuid;
  const auto cached = speaker_volumes_.find(speaker->uuid);
  view_.speaker_volume =
      cached == speaker_volumes_.end() ? -1 : cached->second.volume;
  view_.speaker_muted =
      cached == speaker_volumes_.end() ? false : cached->second.muted;
  request_speaker_volume();
}

void Controller::adjust_speaker_card_volume(int direction) {
  const Player* target = volume_target();
  if (!target) return;
  if (view_.speaker_volume < 0) {
    request_speaker_volume();
    show_toast("Reading " + target->room_name + " volume...");
    return;
  }
  const int intended =
      clamp_volume(view_.speaker_volume + direction * settings_.volume_step);
  view_.speaker_volume = intended;
  const SpeakerVolume volume{intended, view_.speaker_muted};
  speaker_volumes_[target->uuid] = volume;
  view_.speaker_volumes[target->uuid] = volume;
  volume_feedback_until_ms_ = monotonic_ms() + 1200;
  Command command;
  command.type = CommandType::Volume;
  command.player = *target;
  command.value = intended;
  command.flag = false;
  enqueue(std::move(command));
}

void Controller::toggle_speaker_card_mute() {
  const Player* target = volume_target();
  if (!target) return;
  if (view_.speaker_volume < 0) {
    request_speaker_volume();
    show_toast("Reading " + target->room_name + " volume...");
    return;
  }
  view_.speaker_muted = !view_.speaker_muted;
  const SpeakerVolume volume{view_.speaker_volume, view_.speaker_muted};
  speaker_volumes_[target->uuid] = volume;
  view_.speaker_volumes[target->uuid] = volume;
  Command command;
  command.type = CommandType::Mute;
  command.player = *target;
  command.flag = view_.speaker_muted;
  command.value = 0;
  enqueue(std::move(command));
  show_toast(target->room_name + (view_.speaker_muted ? " muted" : " unmuted"));
}

void Controller::sync_speaker_card_volumes() {
  const Group* group = active_group();
  const Player* source = volume_target();
  if (!group || !source) return;
  if (view_.speaker_volume < 0) {
    request_speaker_volume();
    show_toast("Reading " + source->room_name + " volume...");
    return;
  }

  Command command;
  command.type = CommandType::SyncSpeakerVolumes;
  command.value = clamp_volume(view_.speaker_volume);
  std::vector<std::string> target_uuids;
  for (const auto& uuid : group->member_uuids) {
    const Player* speaker = player_by_uuid(uuid);
    if (speaker && speaker->visible && speaker->available) {
      command.players.push_back(*speaker);
      target_uuids.push_back(speaker->uuid);
    }
  }
  if (command.players.size() < 2) {
    show_toast("Only one speaker is in this group.");
    return;
  }
  const int volume = command.value;
  const std::size_t speaker_count = command.players.size();
  if (!enqueue(std::move(command))) return;

  // Keep cards that already have a confirmed mute state responsive while the
  // worker performs the bounded multi-speaker update. A refresh follows the
  // result so unavailable or stale cards are never treated as authoritative.
  for (const auto& uuid : target_uuids) {
    const auto known = speaker_volumes_.find(uuid);
    if (known == speaker_volumes_.end()) continue;
    known->second.volume = volume;
    view_.speaker_volumes[uuid] = known->second;
  }
  volume_feedback_until_ms_ = monotonic_ms() + 1200;
  show_toast("Syncing " + std::to_string(speaker_count) + " speakers...");
}

void Controller::select_group(std::size_t index, bool opened_by_user) {
  if (index >= view_.topology.groups.size()) return;
  const Group& group = view_.topology.groups[index];
  const bool same_active_group = !view_.active_group_id.empty() &&
                                 view_.active_group_id == group.id;
  view_.active_group_id = group.id;
  // Selecting a room/group always lands on the group control in Now Playing.
  view_.group_volume_target = true;
  const auto contains = [&group](const std::string& uuid) {
    return std::find(group.member_uuids.begin(), group.member_uuids.end(), uuid) !=
           group.member_uuids.end();
  };
  if (!contains(view_.active_room_uuid) &&
      contains(settings_.last_room_uuid)) {
    view_.active_room_uuid = settings_.last_room_uuid;
  } else if (!contains(view_.active_room_uuid)) {
    view_.active_room_uuid = group.member_uuids.empty()
                                 ? group.coordinator_uuid
                                 : group.member_uuids.front();
  }
  if (same_active_group && view_.playback.received_at_ms > 0) {
    view_.speaker_volume = view_.playback.volume;
    view_.speaker_muted = view_.playback.muted;
  } else {
    // The old playback snapshot belongs to a different group, so never use
    // it as a volume value for the newly selected group.
    view_.speaker_volume = -1;
    view_.speaker_muted = false;
  }
  if (!same_active_group) {
    // A playlist label belongs to the active group queue. Topology refreshes
    // regularly reselect the same group; they must not erase that label while
    // the queue advances to later tracks.
    selected_playlist_title_.clear();
    selected_playlist_object_id_.clear();
    selected_playlist_artwork_uri_.clear();
    playlist_context_lookup_requested_id_.clear();
    pending_playlist_title_.clear();
    pending_playlist_object_id_.clear();
    playlist_title_before_start_.clear();
    playlist_object_before_start_.clear();
    playlist_artwork_path_before_start_.clear();
    playlist_artwork_title_before_start_.clear();
    playlist_artwork_url_before_start_.clear();
    playlist_artwork_uri_before_start_.clear();
    playlist_start_acknowledged_ = false;
    view_.now_playing_playlist_artwork_path.clear();
    view_.now_playing_playlist_artwork_title.clear();
    last_now_playing_playlist_artwork_url_.clear();
  }
  settings_.last_group_id = group.id;
  settings_.last_room_uuid = view_.active_room_uuid;
  save_settings();
  view_.connected = true;
  if (opened_by_user || !same_active_group) {
    view_.screen = Screen::NowPlaying;
    history_.clear();
  }
  request_poll();
  request_group_speaker_volumes();
}

void Controller::apply_result(WorkerResult result) {
  if (result.type != ResultType::Artwork && result.type != ResultType::CacheCleared) {
    view_.busy = false;
  }
  switch (result.type) {
    case ResultType::Discovery:
      view_.discovering = false;
      if (!result.success) {
        ++discovery_failures_;
        const int shift = std::min(4, discovery_failures_ - 1);
        const uint64_t delay_ms =
            static_cast<uint64_t>(std::min(60, 4 << shift)) * 1000;
        next_discovery_ms_ = monotonic_ms() + delay_ms;
        view_.connected = false;
        view_.screen = Screen::Offline;
        view_.error = result.text;
        view_.selection = 0;
        break;
      }
      discovery_failures_ = 0;
      next_discovery_ms_ = 0;
      view_.topology.players = std::move(result.players);
      settings_.cached_ips.clear();
      for (const auto& player : view_.topology.players) {
        if (valid_ipv4(player.ip)) settings_.cached_ips.push_back(player.ip);
      }
      save_settings();
      view_.status = "Loading rooms...";
      request_topology();
      break;
    case ResultType::Topology: {
      if (!result.success) {
        view_.error = result.text;
        if (view_.topology.groups.empty()) {
          view_.screen = Screen::Offline;
          view_.connected = false;
          ++discovery_failures_;
          next_discovery_ms_ =
              monotonic_ms() +
              static_cast<uint64_t>(std::min(60, 4 << std::min(
                                                           4,
                                                           discovery_failures_ -
                                                               1))) *
                  1000;
        } else {
          show_toast("Topology refresh failed.");
        }
        break;
      }
      const std::string old_group = view_.active_group_id;
      const std::string old_room = view_.active_room_uuid;
      view_.topology = std::move(result.topology);
      persist_topology_snapshot(data_directory_, view_.topology);
      view_.connected = true;
      std::size_t selected = 0;
      bool matched = false;
      auto match_group = [&](const Group& group) {
        if (!old_group.empty() && group.id == old_group) return true;
        if (!old_room.empty() &&
            std::find(group.member_uuids.begin(), group.member_uuids.end(), old_room) !=
                group.member_uuids.end())
          return true;
        if (settings_.startup_mode == StartupMode::SpecificRoom &&
            !settings_.startup_room_uuid.empty() &&
            std::find(group.member_uuids.begin(), group.member_uuids.end(),
                      settings_.startup_room_uuid) != group.member_uuids.end())
          return true;
        return group.id == settings_.last_group_id;
      };
      for (std::size_t i = 0; i < view_.topology.groups.size(); ++i) {
        if (match_group(view_.topology.groups[i])) {
          selected = i;
          matched = true;
          break;
        }
      }
      if (!matched && view_.topology.groups.empty()) {
        view_.screen = Screen::Offline;
        view_.connected = false;
        view_.error = "No controllable Sonos rooms are available.";
        ++discovery_failures_;
        next_discovery_ms_ =
            monotonic_ms() +
            static_cast<uint64_t>(std::min(
                60, 4 << std::min(4, discovery_failures_ - 1))) *
                1000;
        break;
      }
      if (settings_.startup_mode == StartupMode::AskEveryTime &&
          view_.active_group_id.empty()) {
        view_.screen = Screen::Rooms;
        view_.selection = static_cast<int>(selected);
      } else {
        select_group(selected, false);
      }
      if (!old_group.empty() && old_group != view_.active_group_id) {
        show_toast("The selected group changed; a surviving room was selected.");
      }
      if (view_.screen == Screen::Speakers) request_speaker_product_photos();
      break;
    }
    case ResultType::Playback: {
      if (!result.success) {
        ++poll_failures_;
        view_.diagnostics.last_error = result.text;
        view_.error = result.text;
        show_toast("Speaker did not respond.");
        if (poll_failures_ >= 3 && !view_.topology.players.empty()) {
          request_topology();
          poll_failures_ = 0;
        }
        break;
      }
      poll_failures_ = 0;
      // Keep track of whether Sonos itself supplied playlist metadata before
      // Miyonos applies any remembered generic-queue context below. If another
      // controller selects a saved playlist, that live metadata is more
      // authoritative than the locally remembered playlist that came before it.
      const bool sonos_reported_playlist_context =
          !result.playback.playlist_title.empty() ||
          is_saved_playlist_object_id(result.playback.active_playlist_object_id);
      bool needs_playlist_lookup = false;
      const bool playlist_start_pending = !pending_playlist_object_id_.empty();
      if (playlist_start_pending) {
        // A poll already in flight can describe the old source after the user
        // presses A. Keep the selected playlist label visible until Sonos
        // reports the replacement queue (or the same saved-playlist ID).
        const bool replacement_confirmed =
            playlist_start_acknowledged_ &&
            (is_queue_playback(result.playback) ||
             result.playback.active_playlist_object_id ==
                 pending_playlist_object_id_);
        result.playback.playlist_title = pending_playlist_title_;
        selected_playlist_title_ = pending_playlist_title_;
        selected_playlist_object_id_ = pending_playlist_object_id_;
        playlist_context_lookup_requested_id_.clear();
        if (replacement_confirmed) {
          pending_playlist_title_.clear();
          pending_playlist_object_id_.clear();
          playlist_title_before_start_.clear();
          playlist_object_before_start_.clear();
          playlist_artwork_path_before_start_.clear();
          playlist_artwork_title_before_start_.clear();
          playlist_artwork_url_before_start_.clear();
          playlist_artwork_uri_before_start_.clear();
          playlist_start_acknowledged_ = false;
        }
      } else {
        const bool saved_playlist = is_saved_playlist_object_id(
            result.playback.active_playlist_object_id);
        if (!result.playback.playlist_title.empty()) {
          selected_playlist_title_ = result.playback.playlist_title;
          selected_playlist_object_id_ =
              saved_playlist ? result.playback.active_playlist_object_id : "";
          playlist_context_lookup_requested_id_.clear();
        } else if (saved_playlist) {
          const auto saved_playlist_item = std::find_if(
              view_.playlists.begin(), view_.playlists.end(),
              [&result](const BrowseItem& item) {
                return item.id == result.playback.active_playlist_object_id &&
                       !item.title.empty();
              });
          if (saved_playlist_item != view_.playlists.end()) {
            result.playback.playlist_title = saved_playlist_item->title;
          } else if (selected_playlist_object_id_ ==
                         result.playback.active_playlist_object_id) {
            result.playback.playlist_title = selected_playlist_title_;
          } else {
            needs_playlist_lookup =
                playlist_context_lookup_requested_id_ !=
                result.playback.active_playlist_object_id;
          }
          if (!result.playback.playlist_title.empty()) {
            selected_playlist_title_ = result.playback.playlist_title;
            selected_playlist_object_id_ =
                result.playback.active_playlist_object_id;
            playlist_context_lookup_requested_id_.clear();
          }
        } else if (is_queue_playback(result.playback) &&
                   !selected_playlist_title_.empty()) {
          // Saved playlists are loaded into Sonos' queue. Some players omit
          // the saved-playlist metadata for that queue, so retain the title
          // selected in Miyonos for the active queue session.
          result.playback.playlist_title = selected_playlist_title_;
        } else {
          selected_playlist_title_.clear();
          selected_playlist_object_id_.clear();
          selected_playlist_artwork_uri_.clear();
          playlist_context_lookup_requested_id_.clear();
        }
      }
      const std::string old_playlist_object =
          view_.playback.active_playlist_object_id;
      if (is_radio_stream(result.playback.track)) {
        if ((is_technical_media_text(result.playback.track.title) ||
             strip_radio_backend_suffix(result.playback.track.title) !=
                 result.playback.track.title) &&
            !active_station_title_.empty()) {
          result.playback.track.title = active_station_title_;
        }
      } else {
        active_station_title_.clear();
      }
      view_.playback = std::move(result.playback);
      if (view_.playback.playlist_title.empty()) {
        view_.now_playing_playlist_artwork_path.clear();
        view_.now_playing_playlist_artwork_title.clear();
        last_now_playing_playlist_artwork_url_.clear();
      } else if (!view_.now_playing_playlist_artwork_title.empty() &&
                 view_.now_playing_playlist_artwork_title !=
                     view_.playback.playlist_title) {
        // Do not show the previous playlist's cover if Sonos reports a
        // different named playlist outside the Miyonos-started queue session.
        view_.now_playing_playlist_artwork_path.clear();
        view_.now_playing_playlist_artwork_title.clear();
        last_now_playing_playlist_artwork_url_.clear();
      }
      if (is_queue_playback(view_.playback) &&
          settings_.playlist_context_group_id == view_.active_group_id &&
          !settings_.playlist_context_queue_fingerprint.empty()) {
        if (sonos_reported_playlist_context) {
          // The queue now has a live, non-generic identity. Discard the old
          // fallback instead of allowing it to overwrite the current source.
          clear_playlist_context();
        } else {
          validate_playlist_context();
        }
      } else if (!is_queue_playback(view_.playback) &&
                 settings_.playlist_context_group_id == view_.active_group_id &&
                 !settings_.playlist_context_queue_fingerprint.empty()) {
        clear_playlist_context();
      }
      if (view_.group_volume_target) {
        if (monotonic_ms() < volume_feedback_until_ms_ &&
            view_.speaker_volume >= 0) {
          view_.playback.volume = view_.speaker_volume;
        } else {
          view_.speaker_volume = view_.playback.volume;
          view_.speaker_muted = view_.playback.muted;
        }
      }
      view_.connected = true;
      view_.diagnostics.last_success = now_timestamp();
      view_.diagnostics.last_error.clear();
      if (settings_.auto_artwork && !view_.playback.track.artwork_uri.empty()) {
        const Player* selected = coordinator();
        const std::string url =
            selected ? artwork_url(*selected, view_.playback.track.artwork_uri,
                                   settings_.spotify_https_artwork) : "";
        if (!url.empty() && url != last_artwork_url_) {
          const std::string cached = artwork_cache_.find(url);
          if (!cached.empty()) {
            last_artwork_url_ = url;
            view_.artwork_path.clear();
            view_.artwork_path = cached;
          } else if (selected && !artwork_paused_.load()) {
            last_artwork_url_ = url;
            view_.artwork_path.clear();
            Command command;
            command.type = CommandType::DownloadArtwork;
            command.player = *selected;
            command.text = url;
            command.flag = settings_.spotify_https_artwork;
            enqueue(std::move(command));
          }
        } else if (url.empty()) {
          view_.artwork_path.clear();
          last_artwork_url_.clear();
        }
      } else {
        view_.artwork_path.clear();
        last_artwork_url_.clear();
      }
      if (view_.screen == Screen::Queue &&
          old_playlist_object != view_.playback.active_playlist_object_id) {
        request_browse(ListKind::Queue);
      }
      if (needs_playlist_lookup) {
        playlist_context_lookup_requested_id_ =
            view_.playback.active_playlist_object_id;
        request_browse(ListKind::Playlists);
      }
      break;
    }
    case ResultType::SpeakerVolume: {
      if (!result.success) {
        show_toast("Could not read speaker volume.");
        break;
      }
      const SpeakerVolume volume{result.value, result.flag};
      speaker_volumes_[result.context] = volume;
      view_.speaker_volumes[result.context] = volume;
      if (!view_.group_volume_target &&
          view_.active_room_uuid == result.context) {
        view_.speaker_volume = volume.volume;
        view_.speaker_muted = volume.muted;
      }
      break;
    }
    case ResultType::Browse: {
      if (!result.success) {
        if (result.browse_intent == BrowseIntent::ValidatePlaylistContext) {
          playlist_context_validation_pending_ = false;
        }
        view_.error = result.text;
        if (result.browse_intent == BrowseIntent::Display)
          show_toast("Could not load this list.");
        break;
      }
      const std::size_t raw_number_returned = result.browse.number_returned;
      const std::size_t raw_total_matches = result.browse.total_matches;
      if (result.browse_intent == BrowseIntent::ValidatePlaylistContext) {
        playlist_context_validation_pending_ = false;
        if (queue_fingerprint(result.browse.items) ==
            settings_.playlist_context_queue_fingerprint) {
          selected_playlist_title_ = settings_.playlist_context_title;
          selected_playlist_object_id_ = settings_.playlist_context_object_id;
          selected_playlist_artwork_uri_ =
              settings_.playlist_context_artwork_uri;
          view_.playback.playlist_title = selected_playlist_title_;
          restore_playlist_context_artwork();
        } else {
          const bool displaying_remembered_context =
              view_.playback.playlist_title ==
              settings_.playlist_context_title;
          selected_playlist_title_.clear();
          selected_playlist_object_id_.clear();
          selected_playlist_artwork_uri_.clear();
          if (displaying_remembered_context) {
            view_.playback.playlist_title.clear();
            view_.now_playing_playlist_artwork_path.clear();
            view_.now_playing_playlist_artwork_title.clear();
            last_now_playing_playlist_artwork_url_.clear();
          }
          clear_playlist_context();
        }
        break;
      }
      if (result.browse_intent == BrowseIntent::CapturePlaylistContext) {
        const std::string fingerprint = queue_fingerprint(result.browse.items);
        if (!fingerprint.empty() && !selected_playlist_title_.empty()) {
          settings_.playlist_context_group_id = view_.active_group_id;
          settings_.playlist_context_title = selected_playlist_title_;
          settings_.playlist_context_object_id = selected_playlist_object_id_;
          settings_.playlist_context_artwork_uri = selected_playlist_artwork_uri_;
          settings_.playlist_context_queue_fingerprint = fingerprint;
          save_settings();
        }
        break;
      }
      if (result.list_kind == ListKind::Playlists) {
        auto& items = result.browse.items;
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [](const BrowseItem& item) {
                                     return !is_playlist_favorite(item);
                                   }),
                    items.end());
        result.browse.number_returned = items.size();
      }
      const bool playlist_page_empty =
          result.list_kind == ListKind::Playlists && result.browse.items.empty();
      auto* target = result.list_kind == ListKind::Queue
                         ? &view_.queue
                         : result.list_kind == ListKind::Favorites
                               ? &view_.favorites
                               : &view_.playlists;
      const std::string* current_object =
          result.list_kind == ListKind::Queue
              ? &queue_object_
              : result.list_kind == ListKind::Favorites ? &favorites_object_
                                                        : &playlists_object_;
      if (result.context != *current_object) break;
      auto* total = result.list_kind == ListKind::Queue
                        ? &view_.queue_total
                        : result.list_kind == ListKind::Favorites
                              ? &view_.favorites_total
                              : &view_.playlists_total;
      if (result.start_index == 0) {
        *target = std::move(result.browse.items);
        if (result.list_kind == ListKind::Queue) {
          view_.queue_artwork_paths.assign(target->size(), {});
          queue_artwork_urls_.assign(target->size(), {});
          failed_queue_artwork_urls_.clear();
        }
      } else if (result.visible_offset == target->size()) {
        const std::size_t count = result.browse.items.size();
        target->insert(target->end(),
                       std::make_move_iterator(result.browse.items.begin()),
                       std::make_move_iterator(result.browse.items.begin() + count));
        if (result.list_kind == ListKind::Queue) {
          view_.queue_artwork_paths.resize(target->size());
          queue_artwork_urls_.resize(target->size());
        }
      }
      // When a complete page contained a Sonos navigation placeholder that
      // was intentionally filtered by the protocol layer, keep pagination
      // aligned with the visible list instead of fetching a duplicate tail.
      if (result.list_kind == ListKind::Playlists) {
        playlists_next_raw_index_ = result.start_index + raw_number_returned;
        playlists_has_more_ = raw_number_returned > 0 &&
            playlists_next_raw_index_ < raw_total_matches;
        *total = target->size();
      } else {
        *total = result.start_index == 0 &&
                         result.browse.number_returned >= result.browse.total_matches
                     ? target->size()
                     : result.browse.total_matches;
      }
      view_.selection = target->empty() ? 0 : std::min<int>(
                                                   view_.selection,
                                                   static_cast<int>(target->size() - 1));
      if (result.list_kind == ListKind::Favorites) {
        request_selected_favorite_artwork();
      } else if (result.list_kind == ListKind::Playlists) {
        const auto active_playlist = std::find_if(
            view_.playlists.begin(), view_.playlists.end(),
            [this](const BrowseItem& item) {
              return item.id == view_.playback.active_playlist_object_id &&
                     !item.title.empty();
            });
        if (active_playlist != view_.playlists.end()) {
          view_.playback.playlist_title = active_playlist->title;
          selected_playlist_title_ = active_playlist->title;
          selected_playlist_object_id_ = active_playlist->id;
          playlist_context_lookup_requested_id_.clear();
        }
        request_selected_playlist_artwork();
        if (playlist_page_empty && playlists_has_more_) {
          request_browse(ListKind::Playlists, playlists_object_,
                         playlists_next_raw_index_);
        }
      } else if (result.list_kind == ListKind::Queue) {
        request_queue_artwork();
      }
      show_toast(result.list_kind == ListKind::Queue ? "Queue refreshed"
                                                    : "Library refreshed");
      break;
    }
    case ResultType::Action:
      if (result.success) {
        if (result.context == "shuffle") {
          // SetPlayMode has acknowledged the requested state. Keep the
          // on-screen indicator responsive; the following poll remains the
          // authoritative reconciliation with Sonos.
          view_.playback.shuffle = result.flag;
        }
        if (result.replaces_playlist_context) {
          selected_playlist_title_ = result.context;
          selected_playlist_object_id_ = result.context_id;
          playlist_context_lookup_requested_id_.clear();
          if (result.context.empty()) {
            selected_playlist_artwork_uri_.clear();
            view_.playback.playlist_title.clear();
            view_.now_playing_playlist_artwork_path.clear();
            view_.now_playing_playlist_artwork_title.clear();
            last_now_playing_playlist_artwork_url_.clear();
            clear_playlist_context();
          }
          if (result.context_id == pending_playlist_object_id_) {
            playlist_start_acknowledged_ = true;
          }
          if (!result.context.empty()) {
            clear_playlist_context();
            capture_playlist_context();
          }
        }
        if (result.show_now_playing_on_success) {
          navigate(Screen::NowPlaying);
        }
        if (!result.quiet_success) {
          show_toast(result.text.empty() ? "Done" : result.text);
          request_poll();
        }
      } else {
        if (result.replaces_playlist_context &&
            result.context_id == pending_playlist_object_id_) {
          selected_playlist_title_ = playlist_title_before_start_;
          selected_playlist_object_id_ = playlist_object_before_start_;
          view_.playback.playlist_title = playlist_title_before_start_;
          view_.now_playing_playlist_artwork_path =
              playlist_artwork_path_before_start_;
          view_.now_playing_playlist_artwork_title =
              playlist_artwork_title_before_start_;
          last_now_playing_playlist_artwork_url_ =
              playlist_artwork_url_before_start_;
          selected_playlist_artwork_uri_ = playlist_artwork_uri_before_start_;
          pending_playlist_title_.clear();
          pending_playlist_object_id_.clear();
          playlist_title_before_start_.clear();
          playlist_object_before_start_.clear();
          playlist_artwork_path_before_start_.clear();
          playlist_artwork_title_before_start_.clear();
          playlist_artwork_url_before_start_.clear();
          playlist_artwork_uri_before_start_.clear();
          playlist_start_acknowledged_ = false;
          request_poll();
        }
        view_.diagnostics.last_error = result.text;
        show_toast(result.quiet_success
                       ? "Volume update failed."
                       : result.text.empty() ? "Speaker did not respond"
                                             : result.text,
                   2400);
      }
      if (result.context == "speaker-volume-sync") {
        request_group_speaker_volumes();
      } else if (result.context == "group-volume") {
        // The coordinator applies a group-relative change asynchronously to
        // every room. Read both group and room values again rather than
        // inventing per-speaker values locally; this also handles 0/100
        // limits and changes made in another Sonos controller.
        request_poll();
        request_group_speaker_volumes();
      }
      break;
    case ResultType::Artwork:
      {
      std::string* artwork_path = &view_.artwork_path;
      std::string* last_artwork_url = &last_artwork_url_;
      if (result.artwork_target == ArtworkTarget::Favorite) {
        artwork_path = &view_.favorite_artwork_path;
        last_artwork_url = &last_favorite_artwork_url_;
      } else if (result.artwork_target == ArtworkTarget::Playlist) {
        artwork_path = &view_.playlist_artwork_path;
        last_artwork_url = &last_playlist_artwork_url_;
      } else if (result.artwork_target == ArtworkTarget::NowPlayingPlaylist) {
        artwork_path = &view_.now_playing_playlist_artwork_path;
        last_artwork_url = &last_now_playing_playlist_artwork_url_;
      }
      if (result.artwork_target == ArtworkTarget::Queue) {
        if (result.context == queue_artwork_inflight_url_) {
          queue_artwork_inflight_url_.clear();
        }
        if (result.success) {
          for (std::size_t index = 0; index < queue_artwork_urls_.size() &&
                                      index < view_.queue_artwork_paths.size();
               ++index) {
            if (queue_artwork_urls_[index] == result.context) {
              view_.queue_artwork_paths[index] = result.text;
            }
          }
        } else {
          failed_queue_artwork_urls_.insert(result.context);
          view_.diagnostics.last_error = result.text;
        }
        request_queue_artwork();
        break;
      }
      if (result.artwork_target == ArtworkTarget::SpeakerProduct) {
        if (result.context == speaker_product_photo_inflight_url_) {
          speaker_product_photo_inflight_url_.clear();
        }
        if (result.success) {
          for (const auto& [uuid, url] : speaker_product_photo_urls_) {
            if (url == result.context) {
              view_.speaker_product_photo_paths[uuid] = result.text;
            }
          }
        } else {
          failed_speaker_product_photo_urls_.insert(result.context);
          view_.diagnostics.last_error = result.text;
        }
        request_speaker_product_photos();
        break;
      }
      if (result.context != *last_artwork_url) break;
      if (result.success) {
        *artwork_path = result.text;
        if (result.artwork_target == ArtworkTarget::Playlist &&
            result.context == last_now_playing_playlist_artwork_url_) {
          view_.now_playing_playlist_artwork_path = result.text;
        }
      } else {
        view_.diagnostics.last_error = result.text;
        last_artwork_url->clear();
      }
      break;
      }
    case ResultType::CacheCleared:
      view_.artwork_path.clear();
      view_.favorite_artwork_path.clear();
      view_.playlist_artwork_path.clear();
      view_.now_playing_playlist_artwork_path.clear();
      view_.queue_artwork_paths.assign(view_.queue.size(), {});
      view_.speaker_product_photo_paths.clear();
      last_artwork_url_.clear();
      last_favorite_artwork_url_.clear();
      last_playlist_artwork_url_.clear();
      last_now_playing_playlist_artwork_url_.clear();
      queue_artwork_urls_.assign(view_.queue.size(), {});
      queue_artwork_inflight_url_.clear();
      failed_queue_artwork_urls_.clear();
      speaker_product_photo_inflight_url_.clear();
      failed_speaker_product_photo_urls_.clear();
      if (settings_.official_sonos_product_photos) {
        request_speaker_product_photos();
      }
      show_toast(result.success ? "Artwork cache cleared"
                               : "Artwork cache could not be cleared");
      break;
    case ResultType::Failure:
      view_.diagnostics.last_error = result.text;
      show_toast(result.text, 2400);
      break;
  }
  refresh_diagnostics();
}

void Controller::update() {
  const uint64_t now = monotonic_ms();
  update_idle_battery_saver(now);
  WorkerResult result;
  while (results_.try_pop(result)) apply_result(std::move(result));
  if (view_.screen == Screen::Splash && now - view_.last_input_ms > 650) {
    view_.screen = Screen::Discovery;
  }
  if (view_.toast_until_ms && now >= view_.toast_until_ms) {
    view_.toast.clear();
    view_.toast_until_ms = 0;
  }
  if (!view_.connected && !view_.discovering && next_discovery_ms_ != 0 &&
      now >= next_discovery_ms_ && commands_.size() < 2) {
    begin_discovery();
  }
  if (view_.connected && coordinator() &&
      now - last_poll_requested_ms_ >=
          static_cast<uint64_t>(polling_interval_ms()) &&
      commands_.size() < 4) {
    request_poll();
  }
  if (view_.connected && !view_.topology.players.empty() &&
      now - last_topology_requested_ms_ >=
          static_cast<uint64_t>(topology_interval_ms()) &&
      commands_.size() < 4) {
    request_topology();
  }
  if (view_.playback.state == TransportState::Playing &&
      view_.playback.track.elapsed_seconds < view_.playback.track.duration_seconds &&
      view_.playback.received_at_ms > 0) {
    const auto extrapolated = static_cast<int>(
        (now - view_.playback.received_at_ms) / 1000);
    view_.playback.track.elapsed_seconds =
        std::min(view_.playback.track.duration_seconds,
                 view_.playback.track.elapsed_seconds + extrapolated);
    view_.playback.received_at_ms = now;
  }
}

int Controller::polling_interval_ms() const {
  if (view_.idle_battery_saver_active) {
    return view_.playback.state == TransportState::Playing
               ? kIdlePlayingPollIntervalMs
               : kIdlePausedPollIntervalMs;
  }
  if (view_.playback.state == TransportState::Playing) {
    return settings_.polling == PollingIntensity::Responsive
               ? 1000
               : settings_.polling == PollingIntensity::BatterySaver ? 3000 : 1800;
  }
  return settings_.polling == PollingIntensity::Responsive
             ? 3000
             : settings_.polling == PollingIntensity::BatterySaver ? 10000 : 6000;
}

int Controller::topology_interval_ms() const {
  return view_.idle_battery_saver_active ? kIdleTopologyIntervalMs : 30000;
}

void Controller::update_idle_battery_saver(uint64_t now) {
  const bool should_be_active =
      settings_.idle_battery_saver && view_.last_input_ms > 0 &&
      now - view_.last_input_ms >= idle_battery_saver_delay_ms();
  if (should_be_active == view_.idle_battery_saver_active) return;
  view_.idle_battery_saver_active = should_be_active;
  artwork_paused_.store(should_be_active);
  MIYONOS_LOG("power", should_be_active
                           ? "Idle battery saver entered"
                           : "Idle battery saver left");
}

void Controller::show_toast(const std::string& message, uint64_t duration_ms) {
  view_.toast = message;
  view_.toast_until_ms = monotonic_ms() + duration_ms;
}

void Controller::navigate(Screen screen) {
  if (view_.screen == screen) return;
  selections_[view_.screen] = view_.selection;
  history_.push_back(view_.screen);
  view_.screen = screen;
  view_.selection = selections_[screen];
  const auto size = list_size(screen);
  if (size == 0) view_.selection = 0;
  else view_.selection = std::max(0, std::min<int>(
                                         view_.selection,
                                         static_cast<int>(size - 1)));
  if (screen == Screen::Queue) request_browse(ListKind::Queue);
  if (screen == Screen::Favorites) request_browse(ListKind::Favorites);
  if (screen == Screen::Playlists) request_browse(ListKind::Playlists);
  if (screen == Screen::Diagnostics) refresh_diagnostics();
  if (screen == Screen::NowPlaying) {
    view_.group_volume_target = true;
    view_.speaker_volume = view_.playback.received_at_ms > 0
                               ? view_.playback.volume
                               : -1;
    view_.speaker_muted = view_.playback.muted;
    request_poll();
  }
  if (screen == Screen::Speakers) {
    request_group_speaker_volumes();
    request_speaker_product_photos();
    focus_speaker_card();
  }
}

void Controller::switch_queue_playlist() {
  if (view_.screen != Screen::Queue && view_.screen != Screen::Playlists) {
    return;
  }
  selections_[view_.screen] = view_.selection;
  view_.screen = view_.screen == Screen::Queue ? Screen::Playlists
                                                : Screen::Queue;
  view_.selection = selections_[view_.screen];
  const std::size_t size = list_size(view_.screen);
  view_.selection = size == 0 ? 0 : std::max(
      0, std::min<int>(view_.selection, static_cast<int>(size - 1)));
  if (view_.screen == Screen::Queue) request_browse(ListKind::Queue);
  else request_browse(ListKind::Playlists);
}

void Controller::enter_button_mapping() {
  view_.pending_button_mapping = settings_.button_mapping;
  navigate(Screen::ButtonMapping);
}

void Controller::adjust_button_mapping(int direction) {
  if (view_.selection < 0 ||
      view_.selection >= static_cast<int>(kPhysicalButtonCount)) {
    return;
  }
  auto& action = view_.pending_button_mapping[
      static_cast<std::size_t>(view_.selection)];
  action = cycle_mappable_action(action, direction);
}

bool Controller::save_button_mapping() {
  if (!button_mapping_is_safe(view_.pending_button_mapping)) {
    show_toast("Keep Up, Down, Confirm, Back, and Exit assigned.", 2800);
    return false;
  }
  settings_.button_mapping = view_.pending_button_mapping;
  save_settings();
  back();
  show_toast("Button mapping saved");
  return true;
}

void Controller::restore_button_mapping() {
  settings_.button_mapping = kDefaultButtonMapping;
  view_.pending_button_mapping = kDefaultButtonMapping;
  save_settings();
  history_.clear();
  view_.screen = Screen::Settings;
  view_.selection = 14;
  show_toast("Default button mapping restored", 2400);
}

void Controller::back() {
  if (view_.screen == Screen::NowPlaying || history_.empty()) {
    view_.screen = Screen::NowPlaying;
    history_.clear();
    view_.selection = 0;
    view_.group_volume_target = true;
    view_.speaker_volume = view_.playback.received_at_ms > 0
                               ? view_.playback.volume
                               : -1;
    view_.speaker_muted = view_.playback.muted;
    request_poll();
    return;
  }
  selections_[view_.screen] = view_.selection;
  view_.screen = history_.back();
  history_.pop_back();
  view_.selection = selections_[view_.screen];
  if (view_.screen == Screen::NowPlaying) {
    view_.group_volume_target = true;
    view_.speaker_volume = view_.playback.received_at_ms > 0
                               ? view_.playback.volume
                               : -1;
    view_.speaker_muted = view_.playback.muted;
    request_poll();
  }
  if (view_.screen == Screen::Favorites) request_selected_favorite_artwork();
  if (view_.screen == Screen::Playlists) request_selected_playlist_artwork();
}

std::size_t Controller::list_size(Screen screen) const {
  switch (screen) {
    case Screen::Rooms: return view_.topology.groups.size();
    case Screen::GroupEditor:
      return static_cast<std::size_t>(std::count_if(
          view_.topology.players.begin(), view_.topology.players.end(),
          [](const Player& player) { return player.visible; }));
    case Screen::Queue: return view_.queue.size();
    case Screen::Favorites: return view_.favorites.size();
    case Screen::Playlists: return view_.playlists.size();
    case Screen::Speakers: {
      const Group* group = active_group();
      if (!group) return 0;
      return static_cast<std::size_t>(std::count_if(
          group->member_uuids.begin(), group->member_uuids.end(),
          [this](const std::string& uuid) {
            const Player* player = player_by_uuid(uuid);
            return player && player->visible && player->available;
          }));
    }
    case Screen::Menu: return 8;
    case Screen::Settings: return 19;
    case Screen::ButtonMapping: return kPhysicalButtonCount;
    case Screen::Diagnostics: return 3;
    case Screen::Offline: return 3;
    default: return 0;
  }
}

void Controller::move_selection(int delta) {
  const std::size_t size = list_size(view_.screen);
  if (size == 0) {
    view_.selection = 0;
    return;
  }
  view_.selection =
      std::max(0, std::min<int>(static_cast<int>(size - 1),
                               view_.selection + delta));
  if (view_.screen == Screen::Speakers) {
    focus_speaker_card();
    return;
  }
  if (view_.screen == Screen::Queue) request_queue_artwork();
  if (view_.screen == Screen::Favorites) request_selected_favorite_artwork();
  if (view_.screen == Screen::Playlists) request_selected_playlist_artwork();
  if (delta <= 0 || view_.busy ||
      view_.selection < static_cast<int>(size) - 3) {
    return;
  }
  if (view_.screen == Screen::Queue && size < view_.queue_total) {
    request_browse(ListKind::Queue, queue_object_, size);
  } else if (view_.screen == Screen::Favorites &&
             size < view_.favorites_total) {
    request_browse(ListKind::Favorites, favorites_object_, size);
  } else if (view_.screen == Screen::Playlists && playlists_has_more_) {
    request_browse(ListKind::Playlists, playlists_object_,
                   playlists_next_raw_index_);
  }
}

void Controller::activate() {
  switch (view_.screen) {
    case Screen::Rooms:
      select_group(static_cast<std::size_t>(view_.selection));
      break;
    case Screen::Queue:
      if (view_.selection < static_cast<int>(view_.queue.size())) {
        const Player* selected = coordinator();
        if (!selected) break;
        Command command;
        command.type = CommandType::PlayQueue;
        command.player = *selected;
        command.index = static_cast<std::size_t>(view_.selection + 1);
        command.replaces_playlist_context = true;
        view_.busy = enqueue(std::move(command));
      }
      break;
    case Screen::Favorites:
    case Screen::Playlists: {
      const bool from_playlists = view_.screen == Screen::Playlists;
      auto& list = view_.screen == Screen::Favorites ? view_.favorites
                                                     : view_.playlists;
      if (view_.selection >= static_cast<int>(list.size())) break;
      const BrowseItem& item = list[view_.selection];
      if (item.container && !item.playable) {
        request_browse(view_.screen == Screen::Favorites ? ListKind::Favorites
                                                         : ListKind::Playlists,
                       item.id);
      } else {
        const Player* selected = coordinator();
        if (!selected) break;
        const bool playlist_favorite =
            from_playlists || is_playlist_favorite(item);
        const bool large_collection_favorite =
            !playlist_favorite &&
            item.uri.rfind("x-rincon-cpcontainer:", 0) == 0;
        Command command;
        command.type = CommandType::PlayItem;
        command.player = *selected;
        command.item = item;
        command.replace_queue = playlist_favorite;
        command.replaces_playlist_context =
            playlist_favorite || large_collection_favorite;
        command.show_now_playing_on_success = large_collection_favorite;
        if (playlist_favorite) {
          command.playlist_title = item.title;
          command.playlist_object_id = item.id;
        }
        const bool radio_station =
            item.uri.rfind("x-sonosapi-stream:", 0) == 0;
        const bool queued = enqueue(std::move(command));
        view_.busy = queued;
        if (queued && playlist_favorite) {
          // Playlist Favorites load into Sonos' generic queue, whose metadata
          // often does not retain the source playlist title.
          pending_playlist_title_ = item.title;
          pending_playlist_object_id_ = item.id;
          playlist_title_before_start_ = view_.playback.playlist_title;
          playlist_object_before_start_ = selected_playlist_object_id_;
          playlist_artwork_path_before_start_ =
              view_.now_playing_playlist_artwork_path;
          playlist_artwork_title_before_start_ =
              view_.now_playing_playlist_artwork_title;
          playlist_artwork_url_before_start_ =
              last_now_playing_playlist_artwork_url_;
          playlist_artwork_uri_before_start_ = selected_playlist_artwork_uri_;
          playlist_start_acknowledged_ = false;
          selected_playlist_title_ = item.title;
          selected_playlist_object_id_ = item.id;
          selected_playlist_artwork_uri_ = item.artwork_uri;
          view_.playback.playlist_title = item.title;
          request_now_playing_playlist_artwork(item);
          navigate(Screen::NowPlaying);
          show_toast("Starting playlist...", 2400);
          request_poll();
        } else if (queued && radio_station) {
          active_station_title_ = item.title;
          show_toast("Starting station...", 5000);
        } else if (queued && large_collection_favorite) {
          show_toast("Opening collection...", 20000);
        }
      }
      break;
    }
    case Screen::Menu: {
      const Screen targets[] = {
          Screen::Rooms,    Screen::Speakers,   Screen::Queue,
          Screen::Favorites, Screen::Settings,  Screen::Help,
          Screen::About,    Screen::Diagnostics};
      navigate(targets[view_.selection]);
      break;
    }
    case Screen::Settings:
      if (view_.selection == 11) enter_ip_editor();
      else if (view_.selection == 14) enter_button_mapping();
      else if (view_.selection == 15) {
        request_confirmation(PendingConfirmation::ClearArtwork,
                             "Clear Artwork Cache?",
                             "Downloaded artwork will be removed.");
      } else if (view_.selection == 16) {
        request_confirmation(PendingConfirmation::ForgetSystem,
                             "Forget Sonos System?",
                             "Cached rooms and addresses will be removed.");
      } else if (view_.selection == 17) {
        request_confirmation(PendingConfirmation::ResetSettings,
                             "Reset All Settings?",
                             "Miyonos defaults will be restored.");
      } else {
        adjust_setting(1);
      }
      break;
    case Screen::GroupEditor: {
      std::vector<Player*> visible;
      for (auto& player : view_.topology.players)
        if (player.visible) visible.push_back(&player);
      if (view_.selection >= static_cast<int>(visible.size())) break;
      Player& member = *visible[view_.selection];
      Group* group = active_group();
      if (!group) break;
      const bool in_group =
          std::find(group->member_uuids.begin(), group->member_uuids.end(),
                    member.uuid) != group->member_uuids.end();
      Command command;
      command.player = member;
      if (in_group) {
        if (group->member_uuids.size() <= 1) {
          show_toast("A group must keep at least one room.");
          break;
        }
        if (member.uuid == group->coordinator_uuid) {
          show_toast("Remove another room, or select a different group first.");
          break;
        }
        command.type = CommandType::Leave;
      } else {
        command.type = CommandType::Join;
        command.text = group->coordinator_uuid;
      }
      view_.busy = enqueue(std::move(command));
      break;
    }
    case Screen::Diagnostics:
      if (view_.selection == 0) {
        refresh_diagnostics();
        request_poll();
        show_toast("Diagnostics refreshed");
      } else if (view_.selection == 1) {
        request_confirmation(PendingConfirmation::ClearLogs, "Clear Logs?",
                             "Local diagnostic logs will be removed.");
      } else {
        export_diagnostics();
      }
      break;
    case Screen::Offline:
      if (view_.selection == 0) {
        view_.screen = Screen::Discovery;
        begin_discovery();
      } else if (view_.selection == 1) {
        enter_ip_editor();
      } else {
        navigate(Screen::Help);
      }
      break;
    case Screen::IpEditor:
      save_ip_editor();
      break;
    case Screen::ConfirmExit:
      exit_requested_ = true;
      break;
    case Screen::NowPlaying: {
      const Player* selected = coordinator();
      if (!selected) break;
      Command command;
      command.player = *selected;
      command.type = view_.playback.state == TransportState::Playing
                         ? CommandType::Pause
                         : CommandType::Play;
      if (enqueue(std::move(command))) {
        view_.playback.state =
            view_.playback.state == TransportState::Playing
                ? TransportState::Paused
                : TransportState::Playing;
        show_toast(view_.playback.state == TransportState::Playing ? "Playing"
                                                                   : "Paused");
      }
      break;
    }
    default:
      break;
  }
}

void Controller::adjust_setting(int direction) {
  switch (view_.selection) {
    case 0: {
      int value = static_cast<int>(settings_.startup_mode);
      value = (value + direction + 3) % 3;
      settings_.startup_mode = static_cast<StartupMode>(value);
      if (settings_.startup_mode == StartupMode::SpecificRoom) {
        settings_.startup_room_uuid = view_.active_room_uuid;
      }
      break;
    }
    case 1: {
      const int values[] = {1, 2, 3, 5};
      auto found = std::find(std::begin(values), std::end(values),
                             settings_.volume_step);
      int index = found == std::end(values)
                      ? 1
                      : static_cast<int>(found - std::begin(values));
      settings_.volume_step = values[(index + direction + 4) % 4];
      break;
    }
    case 2: {
      const int values[] = {5, 10, 15, 30};
      auto found =
          std::find(std::begin(values), std::end(values), settings_.seek_seconds);
      int index = found == std::end(values)
                      ? 1
                      : static_cast<int>(found - std::begin(values));
      settings_.seek_seconds = values[(index + direction + 4) % 4];
      break;
    }
    case 3:
      settings_.artwork_cache_mb =
          std::max(5, std::min(100, settings_.artwork_cache_mb + direction * 5));
      artwork_cache_.set_maximum_bytes(
          static_cast<std::size_t>(settings_.artwork_cache_mb) * 1024 * 1024);
      break;
    case 4:
      settings_.auto_artwork = !settings_.auto_artwork;
      if (!settings_.auto_artwork) {
        view_.queue_artwork_paths.assign(view_.queue.size(), {});
        queue_artwork_urls_.assign(view_.queue.size(), {});
        failed_queue_artwork_urls_.clear();
      } else {
        request_queue_artwork();
      }
      break;
    case 5:
      settings_.spotify_https_artwork = !settings_.spotify_https_artwork;
      queue_artwork_urls_.assign(view_.queue.size(), {});
      failed_queue_artwork_urls_.clear();
      request_queue_artwork();
      break;
    case 6: {
      settings_.official_sonos_product_photos =
          !settings_.official_sonos_product_photos;
      view_.speaker_product_photo_paths.clear();
      speaker_product_photo_urls_.clear();
      speaker_product_photo_inflight_url_.clear();
      failed_speaker_product_photo_urls_.clear();
      request_speaker_product_photos();
      show_toast(settings_.official_sonos_product_photos
                     ? "Official Sonos photos enabled"
                     : "Official Sonos photos disabled");
      break;
    }
    case 7: {
      int value = static_cast<int>(settings_.polling);
      settings_.polling =
          static_cast<PollingIntensity>((value + direction + 3) % 3);
      break;
    }
    case 8: {
      const int values[] = {0, 30, 60, 120, 300, 600};
      auto found = std::find(std::begin(values), std::end(values),
                             settings_.dim_timeout_seconds);
      int index = found == std::end(values)
                      ? 3
                      : static_cast<int>(found - std::begin(values));
      settings_.dim_timeout_seconds = values[(index + direction + 6) % 6];
      break;
    }
    case 9: settings_.idle_battery_saver = !settings_.idle_battery_saver; break;
    case 10: settings_.prevent_sleep = !settings_.prevent_sleep; break;
    case 12: {
      int value = static_cast<int>(settings_.button_hints);
      settings_.button_hints =
          static_cast<ButtonHints>((value + direction + 3) % 3);
      break;
    }
    case 13: settings_.confirm_exit = !settings_.confirm_exit; break;
    case 18:
      settings_.diagnostics_mode = !settings_.diagnostics_mode;
      Logger::instance().set_verbose(settings_.diagnostics_mode);
      break;
    default: return;
  }
  save_settings();
}

void Controller::save_settings() {
  std::string error;
  if (!settings_store_.save(settings_, &error) && !error.empty()) {
    MIYONOS_WARN("settings", error);
  }
}

void Controller::enter_ip_editor() {
  history_.push_back(view_.screen);
  view_.screen = Screen::IpEditor;
  view_.selection = 0;
  view_.ip_octet = 0;
  if (!settings_.manual_ips.empty()) {
    std::istringstream stream(settings_.manual_ips.front());
    std::string part;
    int index = 0;
    while (std::getline(stream, part, '.') && index < 4) {
      view_.ip_octets[index++] = std::max(0, std::min(255, std::atoi(part.c_str())));
    }
  }
}

void Controller::save_ip_editor() {
  const std::string ip = ip_from_octets(view_.ip_octets);
  if (!valid_ipv4(ip)) {
    show_toast("Enter a valid player IP address.");
    return;
  }
  if (std::find(settings_.manual_ips.begin(), settings_.manual_ips.end(), ip) ==
      settings_.manual_ips.end()) {
    settings_.manual_ips.push_back(ip);
  }
  save_settings();
  show_toast("Player IP saved");
  back();
  if (!view_.connected) {
    view_.screen = Screen::Discovery;
    begin_discovery();
  }
}

void Controller::refresh_diagnostics() {
  view_.diagnostics.version = MIYONOS_VERSION;
  view_.diagnostics.player_count = view_.topology.players.size();
  view_.diagnostics.cache_bytes = artwork_cache_.size_bytes();
  const Player* selected = coordinator();
  view_.diagnostics.selected_coordinator =
      selected ? selected->room_name : "None";
  view_.diagnostics.local_ip = local_ipv4();
  std::ifstream onion("/mnt/SDCARD/.tmp_update/.version");
  if (onion) std::getline(onion, view_.diagnostics.onion_version);
  if (view_.diagnostics.onion_version.empty()) {
    view_.diagnostics.onion_version = "Not detected";
  }
}

void Controller::export_diagnostics() {
  refresh_diagnostics();
  const fs::path path = fs::path(data_directory_) / "miyonos-diagnostics.txt";
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    show_toast("Diagnostic report could not be written.");
    return;
  }
  output << "Miyonos Diagnostic Report\n"
         << "Generated: " << now_timestamp() << "\n"
         << "Miyonos version: " << view_.diagnostics.version << "\n"
         << "OnionOS version: " << view_.diagnostics.onion_version << "\n"
         << "Protocol adapter: " << view_.diagnostics.protocol_version << "\n"
         << "Discovered players: " << view_.diagnostics.player_count << "\n"
         << "Selected room: " << view_.diagnostics.selected_coordinator << "\n"
         << "Last successful refresh: " << view_.diagnostics.last_success << "\n"
         << "Last network error: " << view_.diagnostics.last_error << "\n"
         << "Artwork cache bytes: " << view_.diagnostics.cache_bytes << "\n"
         << "Diagnostics mode: "
         << (settings_.diagnostics_mode ? "on" : "off") << "\n\n"
         << "Unique device identifiers and listening history are intentionally "
            "not exported.\n";
  show_toast("Report saved to the Miyonos data folder.", 2800);
}

void Controller::request_confirmation(PendingConfirmation pending,
                                      const std::string& title,
                                      const std::string& message) {
  pending_confirmation_ = pending;
  view_.confirmation_title = title;
  view_.confirmation_message = message;
  navigate(Screen::ConfirmAction);
}

void Controller::confirm_pending() {
  const auto pending = pending_confirmation_;
  pending_confirmation_ = PendingConfirmation::None;
  if (pending == PendingConfirmation::ClearArtwork) {
    Command command;
    command.type = CommandType::ClearCache;
    enqueue(std::move(command));
    back();
  } else if (pending == PendingConfirmation::ForgetSystem) {
    settings_.cached_ips.clear();
    settings_.last_group_id.clear();
    settings_.last_room_uuid.clear();
    save_settings();
    view_.topology = {};
    view_.connected = false;
    history_.clear();
    view_.screen = Screen::Discovery;
    begin_discovery();
  } else if (pending == PendingConfirmation::ResetSettings) {
    settings_ = Settings{};
    save_settings();
    artwork_cache_.set_maximum_bytes(20 * 1024 * 1024);
    back();
    show_toast("Settings reset");
  } else if (pending == PendingConfirmation::ClearLogs) {
    Logger::instance().clear();
    back();
    show_toast("Logs cleared");
  }
}

void Controller::handle(Action action) {
  if (action == Action::None) return;
  note_user_activity();
  if (action == Action::ResetButtonMapping) {
    restore_button_mapping();
    view_.controls_overlay = false;
    return;
  }
  if (view_.controls_overlay) {
    if (action == Action::Controls || action == Action::Back ||
        action == Action::Confirm) {
      view_.controls_overlay = false;
    } else if (action == Action::ExitButton) {
      view_.controls_overlay = false;
      if (settings_.confirm_exit) navigate(Screen::ConfirmExit);
      else exit_requested_ = true;
    }
    return;
  }
  if (view_.screen == Screen::ConfirmExit) {
    if (action == Action::Back) back();
    else if (action == Action::Confirm || action == Action::ExitButton)
      exit_requested_ = true;
    return;
  }
  if (view_.screen == Screen::ConfirmAction) {
    if (action == Action::Back || action == Action::ExitButton) {
      pending_confirmation_ = PendingConfirmation::None;
      back();
    } else if (action == Action::Confirm) {
      confirm_pending();
    }
    return;
  }
  if (action == Action::Controls) {
    view_.controls_overlay = true;
    return;
  }
  if (view_.screen == Screen::ButtonMapping) {
    if (action == Action::Up) {
      move_selection(-1);
    } else if (action == Action::Down) {
      move_selection(1);
    } else if (action == Action::Left) {
      adjust_button_mapping(-1);
    } else if (action == Action::Right || action == Action::Confirm) {
      adjust_button_mapping(1);
    } else if (action == Action::Context) {
      const auto button = static_cast<PhysicalButton>(view_.selection);
      view_.pending_button_mapping[static_cast<std::size_t>(view_.selection)] =
          default_button_action(button);
      show_toast("Button restored to default");
    } else if (action == Action::Refresh) {
      view_.pending_button_mapping = kDefaultButtonMapping;
      show_toast("All defaults selected");
    } else if (action == Action::Back) {
      save_button_mapping();
    } else if (action == Action::ExitButton) {
      view_.pending_button_mapping = settings_.button_mapping;
      if (settings_.confirm_exit) navigate(Screen::ConfirmExit);
      else exit_requested_ = true;
    } else if (action == Action::Menu) {
      view_.pending_button_mapping = settings_.button_mapping;
      navigate(Screen::Menu);
    } else if (action == Action::Rooms) {
      view_.pending_button_mapping = settings_.button_mapping;
      navigate(Screen::Rooms);
    } else if (action == Action::SpeakerVolumes) {
      view_.pending_button_mapping = settings_.button_mapping;
      navigate(Screen::Speakers);
    }
    return;
  }
  if (action == Action::ExitButton) {
    if (settings_.confirm_exit) navigate(Screen::ConfirmExit);
    else exit_requested_ = true;
    return;
  }
  if (view_.screen == Screen::IpEditor) {
    if (action == Action::Left) view_.ip_octet = std::max(0, view_.ip_octet - 1);
    else if (action == Action::Right)
      view_.ip_octet = std::min(3, view_.ip_octet + 1);
    else if (action == Action::Up)
      view_.ip_octets[view_.ip_octet] =
          (view_.ip_octets[view_.ip_octet] + 1) % 256;
    else if (action == Action::Down)
      view_.ip_octets[view_.ip_octet] =
          (view_.ip_octets[view_.ip_octet] + 255) % 256;
    else if (action == Action::Confirm) save_ip_editor();
    else if (action == Action::Context) {
      settings_.manual_ips.clear();
      save_settings();
      show_toast("Manual player IP addresses cleared");
    }
    else if (action == Action::Back) back();
    return;
  }
  if (action == Action::Menu) {
    navigate(Screen::Menu);
    return;
  }
  if (action == Action::Rooms) {
    navigate(Screen::Rooms);
    return;
  }
  if (action == Action::SpeakerVolumes) {
    navigate(Screen::Speakers);
    return;
  }
  if (action == Action::Back) {
    back();
    return;
  }
  if (action == Action::Refresh) {
    if (view_.screen == Screen::Rooms ||
        view_.screen == Screen::GroupEditor) {
      request_topology();
    } else if (view_.screen == Screen::Speakers) {
      request_group_speaker_volumes();
      show_toast("Refreshing speaker volumes...");
    } else if (view_.screen == Screen::Queue) {
      request_browse(ListKind::Queue);
    } else if (view_.screen == Screen::Favorites) {
      request_browse(ListKind::Favorites);
    } else if (view_.screen == Screen::Playlists) {
      request_browse(ListKind::Playlists);
    } else if (view_.screen == Screen::Offline ||
               view_.screen == Screen::Discovery) {
      begin_discovery();
    } else {
      request_poll();
      request_speaker_volume();
      show_toast("Refreshing...");
    }
    return;
  }

  if (action == Action::Queue && view_.screen == Screen::Playlists) {
    switch_queue_playlist();
    return;
  }

  if (view_.screen == Screen::Speakers) {
    if (action == Action::Left || action == Action::Previous ||
        action == Action::PreviousSpeaker) {
      move_selection(-1);
    } else if (action == Action::Right || action == Action::Next ||
               action == Action::NextSpeaker) {
      move_selection(1);
    } else if (action == Action::Up || action == Action::Down) {
      adjust_speaker_card_volume(action == Action::Up ? 1 : -1);
    } else if (action == Action::Confirm) {
      toggle_speaker_card_mute();
    } else if (action == Action::Context) {
      sync_speaker_card_volumes();
    }
    return;
  }

  if (view_.screen == Screen::NowPlaying) {
    const Player* selected = coordinator();
    if (!selected) return;
    if (action == Action::Confirm) {
      activate();
    } else if (action == Action::NextGroup) {
      cycle_group(1);
    } else if (action == Action::PreviousSpeaker ||
               action == Action::NextSpeaker) {
      cycle_volume_target(action == Action::NextSpeaker ? 1 : -1);
    } else if (action == Action::Up || action == Action::Down) {
      const Player* target = volume_target();
      if (!target) return;
      if (view_.speaker_volume < 0) {
        request_speaker_volume();
        show_toast("Reading " + target->room_name + " volume...");
        return;
      }
      const int delta =
          (action == Action::Up ? settings_.volume_step : -settings_.volume_step);
      const int previous_volume = view_.speaker_volume;
      const int intended = clamp_volume(previous_volume + delta);
      if (intended == previous_volume) return;
      view_.speaker_volume = intended;
      if (view_.group_volume_target && view_.playback.group_volume) {
        view_.playback.volume = intended;
      } else {
        const SpeakerVolume volume{intended, view_.speaker_muted};
        speaker_volumes_[target->uuid] = volume;
        view_.speaker_volumes[target->uuid] = volume;
      }
      volume_feedback_until_ms_ = monotonic_ms() + 1200;
      Command command;
      command.player = *target;
      if (view_.group_volume_target && view_.playback.group_volume) {
        // A D-pad press is an increment, not a slider position. Let Sonos
        // make the matching relative adjustment for every member of the
        // group, retaining each room's individual offset.
        command.type = CommandType::AdjustGroupVolume;
        command.value = intended - previous_volume;
      } else {
        command.type = CommandType::Volume;
        command.value = intended;
        command.flag = false;
      }
      enqueue(std::move(command));
    } else if (action == Action::SeekBackward ||
               action == Action::SeekForward) {
      if (!view_.playback.track.seekable) {
        show_toast("Seeking is not available for this source.");
      } else {
        const int delta = action == Action::SeekForward
                              ? settings_.seek_seconds
                              : -settings_.seek_seconds;
        const int target =
            seek_target(view_.playback.track.elapsed_seconds, delta,
                        view_.playback.track.duration_seconds);
        view_.playback.track.elapsed_seconds = target;
        Command command;
        command.type = CommandType::Seek;
        command.player = *selected;
        command.value = target;
        enqueue(std::move(command));
      }
    } else if (action == Action::Context) {
      const Group* group = active_group();
      if (!group) return;
      view_.playback.muted = !view_.playback.muted;
      Command command;
      command.type = CommandType::Mute;
      command.player = *selected;
      command.flag = view_.playback.muted;
      command.value = group->member_uuids.size() > 1 ? 1 : 0;
      enqueue(std::move(command));
      show_toast(view_.playback.muted ? "Muted" : "Unmuted");
    } else if (action == Action::ToggleShuffle) {
      if (is_radio_stream(view_.playback.track)) {
        show_toast("This radio station cannot be shuffled.");
        return;
      }
      const bool enable_shuffle = !view_.playback.shuffle;
      Command command;
      command.type = CommandType::SetShuffle;
      command.player = *selected;
      command.flag = enable_shuffle;
      if (enqueue(std::move(command))) {
        show_toast(enable_shuffle ? "Enabling shuffle..." : "Disabling shuffle...");
      }
    } else if (action == Action::Previous || action == Action::Next ||
               action == Action::Left || action == Action::Right) {
      if (is_radio_stream(view_.playback.track)) {
        show_toast("This radio station has no next or previous track.");
        return;
      }
      Command command;
      command.type = action == Action::Previous || action == Action::Left
                         ? CommandType::Previous
                         : CommandType::Next;
      command.player = *selected;
      enqueue(std::move(command));
    } else if (action == Action::Queue) {
      navigate(Screen::Queue);
    } else if (action == Action::Favorites) {
      navigate(Screen::Favorites);
    }
    return;
  }

  if (action == Action::Up) move_selection(-1);
  else if (action == Action::Down) move_selection(1);
  else if (action == Action::Left) {
    if (view_.screen == Screen::Settings) adjust_setting(-1);
    else move_selection(-6);
  } else if (action == Action::Right) {
    if (view_.screen == Screen::Settings) adjust_setting(1);
    else move_selection(6);
  } else if (action == Action::Previous ||
             action == Action::PreviousSpeaker) {
    move_selection(-20);
  } else if (action == Action::Next || action == Action::NextSpeaker) {
    move_selection(20);
  } else if (action == Action::Confirm) {
    activate();
  } else if (action == Action::Context) {
    if (view_.screen == Screen::Rooms) navigate(Screen::GroupEditor);
    else if (view_.screen == Screen::Queue || view_.screen == Screen::Playlists)
      switch_queue_playlist();
  }
}

void Controller::worker_loop() {
  SonosAdapter adapter(&cancelled_);
  std::vector<Player> known_players;
  Topology topology;
  while (!cancelled_.load()) {
    Command command;
    if (!commands_.wait_pop(command, 200)) continue;
    if (command.type == CommandType::Stop) break;
    WorkerResult result;
    auto action = [&](const ProtocolResult<bool>& response,
                      const std::string& success_text) {
      result.type = ResultType::Action;
      result.success = response.ok();
      result.text = response.ok() ? success_text : response.error;
    };
    switch (command.type) {
      case CommandType::Discover: {
        auto response = adapter.discover(command.ips);
        result.type = ResultType::Discovery;
        result.success = response.ok();
        result.text = response.error;
        result.players = response.value;
        if (response.ok()) known_players = response.value;
        break;
      }
      case CommandType::RefreshTopology: {
        auto response = adapter.get_topology(command.player, known_players);
        result.type = ResultType::Topology;
        result.success = response.ok();
        result.text = response.error;
        result.topology = response.value;
        if (response.ok()) {
          topology = response.value;
          known_players = topology.players;
        }
        break;
      }
      case CommandType::Poll: {
        auto response = adapter.get_playback(command.player);
        result.type = ResultType::Playback;
        result.success = response.ok();
        result.text = response.error;
        result.playback = response.value;
        break;
      }
      case CommandType::Play: action(adapter.play(command.player), "Playing"); break;
      case CommandType::Pause:
        action(adapter.pause(command.player), "Paused");
        break;
      case CommandType::Previous:
        action(adapter.previous(command.player), "Previous track");
        break;
      case CommandType::Next:
        action(adapter.next(command.player), "Next track");
        break;
      case CommandType::SetShuffle:
        action(adapter.set_shuffle(command.player, command.flag),
               command.flag ? "Shuffle enabled" : "Shuffle disabled");
        result.context = "shuffle";
        result.flag = command.flag;
        break;
      case CommandType::Seek:
        action(adapter.seek_time(command.player, command.value), "Position changed");
        break;
      case CommandType::Volume:
        action(adapter.set_volume(command.player, command.value, command.flag),
               "");
        result.quiet_success = true;
        break;
      case CommandType::AdjustGroupVolume:
        action(adapter.adjust_group_volume(command.player, command.value), "");
        result.context = "group-volume";
        result.quiet_success = true;
        break;
      case CommandType::SyncSpeakerVolumes: {
        result.type = ResultType::Action;
        result.context = "speaker-volume-sync";
        result.success = true;
        for (const Player& speaker : command.players) {
          if (cancelled_.load()) {
            result.success = false;
            result.text = "Speaker-volume sync was cancelled.";
            break;
          }
          const auto response = adapter.set_volume(speaker, command.value, false);
          if (!response.ok()) {
            result.success = false;
            result.text = "Could not sync every speaker volume.";
            break;
          }
        }
        if (result.success) result.text = "All speaker volumes synced";
        break;
      }
      case CommandType::GetSpeakerVolume: {
        const auto response = adapter.get_speaker_volume(command.player);
        result.type = ResultType::SpeakerVolume;
        result.success = response.ok();
        result.text = response.error;
        result.context = command.player.uuid;
        result.value = response.value.volume;
        result.flag = response.value.muted;
        break;
      }
      case CommandType::Mute:
        action(adapter.set_mute(command.player, command.flag, command.value != 0),
               command.flag ? "Muted" : "Unmuted");
        break;
      case CommandType::Browse: {
        auto response =
            adapter.browse(command.player, command.text, command.index,
                           kBrowsePageSize);
        result.type = ResultType::Browse;
        result.success = response.ok();
        result.text = response.error;
        result.browse = std::move(response.value);
        result.list_kind = command.list_kind;
        result.browse_intent = command.browse_intent;
        result.context = command.text;
        result.start_index = command.index;
        result.visible_offset = command.visible_offset;
        break;
      }
      case CommandType::PlayQueue:
        action(adapter.play_queue_item(command.player, command.index),
               "Queue item started");
        result.replaces_playlist_context = command.replaces_playlist_context;
        result.context = command.playlist_title;
        result.context_id = command.playlist_object_id;
        break;
      case CommandType::PlayItem:
        action(adapter.play_item(command.player, command.item,
                                 command.replace_queue),
               command.show_now_playing_on_success
                   ? "Opening collection..."
                   : command.item.uri.rfind("x-sonosapi-stream:", 0) == 0
                         ? "Station started"
                         : "Item started");
        result.replaces_playlist_context = command.replaces_playlist_context;
        result.show_now_playing_on_success =
            command.show_now_playing_on_success;
        result.context = command.playlist_title;
        result.context_id = command.playlist_object_id;
        break;
      case CommandType::PlaySavedPlaylist:
        action(adapter.play_saved_playlist(command.player, command.item),
               "Saved playlist started");
        result.replaces_playlist_context = command.replaces_playlist_context;
        result.context = command.playlist_title;
        result.context_id = command.playlist_object_id;
        break;
      case CommandType::Join:
        action(adapter.join_group(command.player, command.text),
               command.player.room_name + " joined the group");
        break;
      case CommandType::Leave:
        action(adapter.leave_group(command.player),
               command.player.room_name + " left the group");
        break;
      case CommandType::ClearCache:
      case CommandType::DownloadArtwork:
        break;
      case CommandType::Stop:
        break;
    }
    results_.try_push(std::move(result));
    if ((command.type == CommandType::Join ||
         command.type == CommandType::Leave) &&
        !known_players.empty() && !cancelled_.load()) {
      auto refreshed = adapter.get_topology(known_players.front(), known_players);
      WorkerResult topology_result;
      topology_result.type = ResultType::Topology;
      topology_result.success = refreshed.ok();
      topology_result.text = refreshed.error;
      topology_result.topology = refreshed.value;
      if (refreshed.ok()) {
        topology = refreshed.value;
        known_players = topology.players;
      }
      results_.try_push(std::move(topology_result));
    }
  }
}

void Controller::artwork_worker_loop() {
  HttpClient http(&cancelled_);
  while (!cancelled_.load()) {
    Command command;
    if (!artwork_commands_.wait_pop(command, 200)) continue;
    if (command.type == CommandType::Stop) break;
    // Keep queued artwork off the network while the user is away. A download
    // that was already inside the HTTP client is allowed to finish safely;
    // subsequent cached-cover requests wait here until the next button press.
    while (command.type == CommandType::DownloadArtwork &&
           artwork_paused_.load() && !cancelled_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (cancelled_.load()) break;
    WorkerResult result;
    if (command.type == CommandType::ClearCache) {
      result.type = ResultType::CacheCleared;
      result.success = artwork_cache_.clear();
    } else if (command.type == CommandType::DownloadArtwork) {
      HttpClient::Limits limits;
      limits.connect_timeout_ms = 4000;
      limits.read_timeout_ms = 8000;
      limits.max_body_bytes = 8 * 1024 * 1024;
      const auto response = http.get_artwork(command.text, command.flag, limits);
      result.type = ResultType::Artwork;
      result.context = command.text;
      result.artwork_target = command.artwork_target;
      result.success = response.ok() &&
          artwork_cache_.store(command.text, response.body, &result.text);
      if (!result.success) {
        if (result.text.empty()) {
          result.text = response.ok() ? "Artwork cache rejected the image"
                                      : response.error;
        }
        MIYONOS_WARN("artwork", "Cover download failed: " + result.text);
      }
    } else {
      continue;
    }
    results_.try_push(std::move(result));
  }
}

}  // namespace miyonos
