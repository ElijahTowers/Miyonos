#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace miyonos {

// Small native Sonos/UPnP fixture embedded only in desktop simulator builds.
// It intentionally mirrors the test fixture's public behavior, so the app
// bundle does not require Python or access to a real Sonos installation.
class SimulatorSonosFixture {
 public:
  explicit SimulatorSonosFixture(std::string scenario);
  ~SimulatorSonosFixture();

  SimulatorSonosFixture(const SimulatorSonosFixture&) = delete;
  SimulatorSonosFixture& operator=(const SimulatorSonosFixture&) = delete;

  bool start();
  void stop();
  const std::string& error() const { return error_; }

 private:
  void serve();
  void handle_client(int client);
  std::string response_for(const std::string& method, const std::string& path,
                           const std::string& headers,
                           const std::string& body, int& status,
                           std::string& content_type);

  std::string scenario_;
  std::string error_;
  std::atomic<bool> running_{false};
  int listen_socket_ = -1;
  std::thread thread_;
  bool playing_ = true;
  bool muted_ = false;
  int volume_ = 28;
  bool queue_cleared_ = false;
  int loaded_playlist_id_ = 0;
  bool coordinator_changed_ = false;
};

}  // namespace miyonos
