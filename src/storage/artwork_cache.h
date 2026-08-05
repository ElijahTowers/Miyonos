#pragma once

#include <atomic>
#include <cstddef>
#include <string>

namespace miyonos {

class ArtworkCache {
 public:
  ArtworkCache(std::string directory, std::size_t maximum_bytes);
  std::string key_for(const std::string& url) const;
  std::string find(const std::string& url);
  bool store(const std::string& url, const std::string& bytes,
             std::string* stored_path = nullptr);
  void evict();
  bool clear();
  std::size_t size_bytes() const;
  void set_maximum_bytes(std::size_t value);
  const std::string& directory() const { return directory_; }

 private:
  bool valid_image(const std::string& bytes) const;
  std::string directory_;
  std::atomic<std::size_t> maximum_bytes_;
};

}  // namespace miyonos
