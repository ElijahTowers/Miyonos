#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace miyonos {

class Logger {
 public:
  enum class Level { Info, Verbose, Warning, Error };

  static Logger& instance();
  void initialize(const std::string& directory, bool verbose = false);
  void log(Level level, const std::string& component, const std::string& message);
  void set_verbose(bool enabled);
  void clear();
  std::string directory() const;

 private:
  Logger() = default;
  void rotate_locked();
  std::string timestamp() const;

  mutable std::mutex mutex_;
  std::ofstream file_;
  std::string directory_;
  std::string file_path_;
  bool verbose_ = false;
};

#define MIYONOS_LOG(component, message) \
  ::miyonos::Logger::instance().log(::miyonos::Logger::Level::Info, component, message)
#define MIYONOS_WARN(component, message) \
  ::miyonos::Logger::instance().log(::miyonos::Logger::Level::Warning, component, message)
#define MIYONOS_ERROR(component, message) \
  ::miyonos::Logger::instance().log(::miyonos::Logger::Level::Error, component, message)
#define MIYONOS_VERBOSE(component, message) \
  ::miyonos::Logger::instance().log(::miyonos::Logger::Level::Verbose, component, message)

}  // namespace miyonos
