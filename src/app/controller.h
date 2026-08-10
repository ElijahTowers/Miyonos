#pragma once

#include <array>
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "domain/types.h"
#include "platform/action.h"
#include "sonos/protocol.h"
#include "storage/artwork_cache.h"
#include "storage/settings.h"
#include "util/bounded_queue.h"

namespace miyonos {

enum class Screen {
  Splash,
  Discovery,
  NowPlaying,
  Rooms,
  GroupEditor,
  Speakers,
  Queue,
  Favorites,
  Playlists,
  Menu,
  Settings,
  ButtonMapping,
  IpEditor,
  Help,
  About,
  Diagnostics,
  ConfirmAction,
  ConfirmExit,
  Offline
};

enum class ListKind { Queue, Favorites, Playlists };
enum class BrowseIntent {
  Display,
  CapturePlaylistContext,
  ValidatePlaylistContext
};

struct ViewState {
  Screen screen = Screen::Splash;
  Topology topology;
  PlaybackSnapshot playback;
  std::vector<BrowseItem> queue;
  std::vector<std::string> queue_artwork_paths;
  std::vector<BrowseItem> favorites;
  std::vector<BrowseItem> playlists;
  std::size_t queue_total = 0;
  std::size_t favorites_total = 0;
  std::size_t playlists_total = 0;
  std::string active_group_id;
  std::string active_room_uuid;
  bool group_volume_target = true;
  int speaker_volume = -1;
  bool speaker_muted = false;
  std::map<std::string, SpeakerVolume> speaker_volumes;
  std::map<std::string, std::string> speaker_product_photo_paths;
  std::string status = "Starting Miyonos...";
  std::string toast;
  std::string error;
  std::string artwork_path;
  std::string favorite_artwork_path;
  std::string playlist_artwork_path;
  std::string now_playing_playlist_artwork_path;
  std::string now_playing_playlist_artwork_title;
  bool discovering = true;
  bool connected = false;
  bool busy = false;
  bool idle_battery_saver_active = false;
  bool controls_overlay = false;
  int selection = 0;
  std::array<int, 4> ip_octets{{192, 168, 1, 100}};
  int ip_octet = 0;
  uint64_t toast_until_ms = 0;
  uint64_t last_input_ms = 0;
  DiagnosticState diagnostics;
  std::string confirmation_title;
  std::string confirmation_message;
  ButtonMapping pending_button_mapping = kDefaultButtonMapping;
};

class Controller {
 public:
  explicit Controller(std::string data_directory);
  ~Controller();

  void start();
  void stop();
  void update();
  void handle(Action action);
  // Called for every physical button press, including a button deliberately
  // mapped to no action, so the idle screen wakes immediately.
  void note_user_activity();
  void record_input_code(int code) { view_.diagnostics.last_input_code = code; }
  const ViewState& view() const { return view_; }
  const Settings& settings() const { return settings_; }
  bool exit_requested() const { return exit_requested_; }
  const std::string& data_directory() const { return data_directory_; }

 private:
  enum class CommandType {
    Discover,
    RefreshTopology,
    Poll,
    Play,
    Pause,
    Previous,
    Next,
    Seek,
    Volume,
    AdjustGroupVolume,
    SyncSpeakerVolumes,
    GetSpeakerVolume,
    Mute,
    Browse,
    PlayQueue,
    PlayItem,
    PlaySavedPlaylist,
    Join,
    Leave,
    DownloadArtwork,
    ClearCache,
    Stop
  };

  enum class PendingConfirmation {
    None,
    ClearArtwork,
    ForgetSystem,
    ResetSettings,
    ClearLogs
  };

  enum class ArtworkTarget {
    NowPlaying,
    Favorite,
    Playlist,
    NowPlayingPlaylist,
    Queue,
    SpeakerProduct
  };

  struct Command {
    CommandType type = CommandType::Poll;
    Player player;
    BrowseItem item;
    std::vector<Player> players;
    ListKind list_kind = ListKind::Queue;
    BrowseIntent browse_intent = BrowseIntent::Display;
    std::string text;
    std::vector<std::string> ips;
    std::string playlist_title;
    std::string playlist_object_id;
    int value = 0;
    bool flag = false;
    bool replace_queue = false;
    bool replaces_playlist_context = false;
    bool show_now_playing_on_success = false;
    ArtworkTarget artwork_target = ArtworkTarget::NowPlaying;
    std::size_t index = 0;
    std::size_t visible_offset = 0;
  };

  enum class ResultType {
    Discovery,
    Topology,
    Playback,
    SpeakerVolume,
    Browse,
    Action,
    Artwork,
    CacheCleared,
    Failure
  };

  struct WorkerResult {
    ResultType type = ResultType::Action;
    std::vector<Player> players;
    Topology topology;
    PlaybackSnapshot playback;
    BrowsePage browse;
    ListKind list_kind = ListKind::Queue;
    BrowseIntent browse_intent = BrowseIntent::Display;
    std::string text;
    std::string context;
    std::string context_id;
    std::size_t start_index = 0;
    std::size_t visible_offset = 0;
    int value = 0;
    bool flag = false;
    bool success = false;
    bool quiet_success = false;
    bool replaces_playlist_context = false;
    bool show_now_playing_on_success = false;
    ArtworkTarget artwork_target = ArtworkTarget::NowPlaying;
  };

  void worker_loop();
  void artwork_worker_loop();
  void apply_result(WorkerResult result);
  bool enqueue(Command command);
  void begin_discovery();
  void request_topology();
  void request_poll();
  void request_speaker_volume();
  void request_group_speaker_volumes();
  void request_speaker_product_photos();
  void request_selected_favorite_artwork();
  void request_selected_playlist_artwork();
  void request_now_playing_playlist_artwork(const BrowseItem& playlist);
  void request_queue_artwork();
  void request_browse(ListKind kind, const std::string& object_id = {},
                      std::size_t start_index = 0,
                      BrowseIntent intent = BrowseIntent::Display);
  void capture_playlist_context();
  void validate_playlist_context();
  void restore_playlist_context_artwork();
  void clear_playlist_context();
  void select_group(std::size_t index, bool opened_by_user = true);
  Player* player_by_uuid(const std::string& uuid);
  const Player* player_by_uuid(const std::string& uuid) const;
  Group* active_group();
  const Group* active_group() const;
  const Player* coordinator() const;
  const Player* volume_target() const;
  void cycle_volume_target(int direction);
  void cycle_group(int direction);
  void focus_speaker_card();
  void adjust_speaker_card_volume(int direction);
  void toggle_speaker_card_mute();
  void sync_speaker_card_volumes();
  void navigate(Screen screen);
  void switch_queue_playlist();
  void back();
  void activate();
  void adjust_setting(int direction);
  void enter_button_mapping();
  void adjust_button_mapping(int direction);
  bool save_button_mapping();
  void restore_button_mapping();
  void show_toast(const std::string& message, uint64_t duration_ms = 1600);
  void save_settings();
  void enter_ip_editor();
  void save_ip_editor();
  std::size_t list_size(Screen screen) const;
  void move_selection(int delta);
  void export_diagnostics();
  void refresh_diagnostics();
  int polling_interval_ms() const;
  int topology_interval_ms() const;
  void update_idle_battery_saver(uint64_t now);
  void request_confirmation(PendingConfirmation pending,
                            const std::string& title,
                            const std::string& message);
  void confirm_pending();

  std::string data_directory_;
  SettingsStore settings_store_;
  Settings settings_;
  ArtworkCache artwork_cache_;
  ViewState view_;
  std::vector<Screen> history_;
  BoundedQueue<Command> commands_{32};
  BoundedQueue<Command> artwork_commands_{8};
  BoundedQueue<WorkerResult> results_{64};
  std::atomic<bool> cancelled_{false};
  std::atomic<bool> artwork_paused_{false};
  std::thread worker_;
  std::thread artwork_worker_;
  bool exit_requested_ = false;
  uint64_t last_poll_requested_ms_ = 0;
  uint64_t last_topology_requested_ms_ = 0;
  uint64_t volume_feedback_until_ms_ = 0;
  uint64_t next_discovery_ms_ = 0;
  int discovery_failures_ = 0;
  int poll_failures_ = 0;
  std::string selected_playlist_title_;
  std::string selected_playlist_object_id_;
  std::string selected_playlist_artwork_uri_;
  std::string playlist_context_lookup_requested_id_;
  std::string pending_playlist_title_;
  std::string pending_playlist_object_id_;
  std::string playlist_title_before_start_;
  std::string playlist_object_before_start_;
  std::string playlist_artwork_path_before_start_;
  std::string playlist_artwork_title_before_start_;
  std::string playlist_artwork_url_before_start_;
  std::string playlist_artwork_uri_before_start_;
  bool playlist_start_acknowledged_ = false;
  std::string active_station_title_;
  std::string last_artwork_url_;
  std::string last_favorite_artwork_url_;
  std::string last_playlist_artwork_url_;
  std::string last_now_playing_playlist_artwork_url_;
  std::vector<std::string> queue_artwork_urls_;
  std::string queue_artwork_inflight_url_;
  std::set<std::string> failed_queue_artwork_urls_;
  std::size_t playlists_next_raw_index_ = 0;
  bool playlists_has_more_ = false;
  bool playlist_context_validation_pending_ = false;
  uint64_t last_playlist_context_validation_ms_ = 0;
  std::map<std::string, SpeakerVolume> speaker_volumes_;
  std::map<std::string, std::string> speaker_product_photo_urls_;
  std::string speaker_product_photo_inflight_url_;
  std::set<std::string> failed_speaker_product_photo_urls_;
  std::map<Screen, int> selections_;
  std::string queue_object_ = "Q:0";
  std::string favorites_object_ = "FV:2";
  std::string playlists_object_ = "FV:2";
  PendingConfirmation pending_confirmation_ = PendingConfirmation::None;
};

}  // namespace miyonos
