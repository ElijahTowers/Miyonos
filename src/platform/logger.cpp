#include "platform/logger.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace miyonos {

namespace {
constexpr std::uintmax_t kMaxLogBytes = 256 * 1024;
}

Logger& Logger::instance() {
  static Logger logger;
  return logger;
}

void Logger::initialize(const std::string& directory, bool verbose) {
  std::lock_guard<std::mutex> lock(mutex_);
  directory_ = directory;
  verbose_ = verbose;
  std::error_code ec;
  fs::create_directories(directory_, ec);
  file_path_ = (fs::path(directory_) / "miyonos.log").string();
  rotate_locked();
  file_.open(file_path_, std::ios::app);
}

void Logger::rotate_locked() {
  std::error_code ec;
  if (!fs::exists(file_path_, ec) || fs::file_size(file_path_, ec) < kMaxLogBytes) return;
  const auto old = (fs::path(directory_) / "miyonos.log.1").string();
  fs::remove(old, ec);
  fs::rename(file_path_, old, ec);
}

std::string Logger::timestamp() const {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

void Logger::log(Level level, const std::string& component, const std::string& message) {
  if (level == Level::Verbose && !verbose_) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_.is_open() && file_.tellp() >= static_cast<std::streampos>(kMaxLogBytes)) {
    file_.close();
    rotate_locked();
    file_.open(file_path_, std::ios::app);
  }
  const char* names[] = {"INFO", "VERBOSE", "WARN", "ERROR"};
  std::ostringstream line;
  line << timestamp() << " [" << names[static_cast<int>(level)] << "] ["
       << component << "] " << message << '\n';
  if (file_.is_open()) {
    file_ << line.str();
    file_.flush();
  }
  std::fputs(line.str().c_str(), stderr);
}

void Logger::set_verbose(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  verbose_ = enabled;
}

void Logger::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  file_.close();
  std::error_code ec;
  fs::remove(file_path_, ec);
  fs::remove((fs::path(directory_) / "miyonos.log.1"), ec);
  file_.open(file_path_, std::ios::trunc);
}

std::string Logger::directory() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return directory_;
}

}  // namespace miyonos
