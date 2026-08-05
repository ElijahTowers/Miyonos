#include "storage/artwork_cache.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace miyonos {

ArtworkCache::ArtworkCache(std::string directory, std::size_t maximum_bytes)
    : directory_(std::move(directory)), maximum_bytes_(maximum_bytes) {
  std::error_code ec;
  fs::create_directories(directory_, ec);
}

std::string ArtworkCache::key_for(const std::string& url) const {
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char c : url) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  std::ostringstream value;
  value << std::hex << std::setfill('0') << std::setw(16) << hash << ".img";
  return value.str();
}

bool ArtworkCache::valid_image(const std::string& bytes) const {
  if (bytes.size() < 16 || bytes.size() > 8 * 1024 * 1024) return false;
  const bool png = static_cast<unsigned char>(bytes[0]) == 0x89 &&
                   bytes.compare(1, 3, "PNG") == 0 &&
                   bytes.compare(12, 4, "IHDR") == 0;
  const bool jpeg = static_cast<unsigned char>(bytes[0]) == 0xff &&
                    static_cast<unsigned char>(bytes[1]) == 0xd8 &&
                    static_cast<unsigned char>(bytes[bytes.size() - 2]) == 0xff &&
                    static_cast<unsigned char>(bytes.back()) == 0xd9;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  if (png && bytes.size() >= 24) {
    auto byte = [&](std::size_t index) {
      return static_cast<std::uint32_t>(
          static_cast<unsigned char>(bytes[index]));
    };
    width = (byte(16) << 24) | (byte(17) << 16) | (byte(18) << 8) | byte(19);
    height = (byte(20) << 24) | (byte(21) << 16) | (byte(22) << 8) | byte(23);
  } else if (jpeg) {
    std::size_t position = 2;
    while (position + 9 < bytes.size()) {
      if (static_cast<unsigned char>(bytes[position]) != 0xff) {
        ++position;
        continue;
      }
      const unsigned char marker =
          static_cast<unsigned char>(bytes[position + 1]);
      if (marker == 0xd8 || marker == 0xd9) {
        position += 2;
        continue;
      }
      const std::size_t segment =
          (static_cast<unsigned char>(bytes[position + 2]) << 8) |
          static_cast<unsigned char>(bytes[position + 3]);
      if (segment < 2 || position + 2 + segment > bytes.size()) break;
      if ((marker >= 0xc0 && marker <= 0xc3) ||
          (marker >= 0xc5 && marker <= 0xc7) ||
          (marker >= 0xc9 && marker <= 0xcb) ||
          (marker >= 0xcd && marker <= 0xcf)) {
        height =
            (static_cast<unsigned char>(bytes[position + 5]) << 8) |
            static_cast<unsigned char>(bytes[position + 6]);
        width =
            (static_cast<unsigned char>(bytes[position + 7]) << 8) |
            static_cast<unsigned char>(bytes[position + 8]);
        break;
      }
      position += 2 + segment;
    }
  }
  constexpr std::uint32_t kMaximumDimension = 2048;
  constexpr std::uint64_t kMaximumPixels = 4ULL * 1024 * 1024;
  return width > 0 && height > 0 && width <= kMaximumDimension &&
         height <= kMaximumDimension &&
         static_cast<std::uint64_t>(width) * height <= kMaximumPixels;
}

std::string ArtworkCache::find(const std::string& url) {
  const fs::path path = fs::path(directory_) / key_for(url);
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return {};
  const auto size = fs::file_size(path, ec);
  if (ec || size < 16 || size > 8 * 1024 * 1024) {
    fs::remove(path, ec);
    return {};
  }
  std::ifstream input(path, std::ios::binary);
  std::string bytes(static_cast<std::size_t>(size), '\0');
  input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!input || !valid_image(bytes)) {
    fs::remove(path, ec);
    return {};
  }
  fs::last_write_time(path, fs::file_time_type::clock::now(), ec);
  return path.string();
}

bool ArtworkCache::store(const std::string& url, const std::string& bytes,
                         std::string* stored_path) {
  if (!valid_image(bytes) || maximum_bytes_.load() == 0) return false;
  std::error_code ec;
  fs::create_directories(directory_, ec);
  if (ec) return false;
  const fs::path destination = fs::path(directory_) / key_for(url);
  const fs::path temporary =
      fs::path(directory_) /
      (key_for(url) + ".tmp." +
       std::to_string(static_cast<long long>(getpid())));
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
      output.close();
      fs::remove(temporary, ec);
      return false;
    }
  }
  fs::rename(temporary, destination, ec);
  if (ec) {
    fs::remove(temporary, ec);
    return false;
  }
  evict();
  if (stored_path) *stored_path = destination.string();
  return true;
}

std::size_t ArtworkCache::size_bytes() const {
  std::size_t total = 0;
  std::error_code ec;
  if (!fs::exists(directory_, ec)) return 0;
  for (const auto& entry : fs::directory_iterator(directory_, ec)) {
    if (entry.is_regular_file(ec) && entry.path().extension() == ".img") {
      const auto size = entry.file_size(ec);
      if (!ec && size <= 8 * 1024 * 1024) total += static_cast<std::size_t>(size);
    }
  }
  return total;
}

void ArtworkCache::evict() {
  struct Entry {
    fs::path path;
    fs::file_time_type time;
    std::uintmax_t size;
  };
  std::vector<Entry> entries;
  std::uintmax_t total = 0;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(directory_, ec)) {
    if (!entry.is_regular_file(ec) || entry.path().extension() != ".img") continue;
    const auto size = entry.file_size(ec);
    if (ec) continue;
    total += size;
    entries.push_back({entry.path(), entry.last_write_time(ec), size});
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry& left, const Entry& right) {
              return left.time < right.time;
            });
  for (const auto& entry : entries) {
    if (total <= maximum_bytes_.load()) break;
    fs::remove(entry.path, ec);
    if (!ec) total -= entry.size;
  }
}

bool ArtworkCache::clear() {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(directory_, ec)) {
    const auto filename = entry.path().filename().string();
    if (entry.is_regular_file(ec) &&
        (entry.path().extension() == ".img" ||
         filename.find(".tmp.") != std::string::npos)) {
      fs::remove(entry.path(), ec);
      if (ec) return false;
    }
  }
  return true;
}

void ArtworkCache::set_maximum_bytes(std::size_t value) {
  maximum_bytes_.store(value);
  evict();
}

}  // namespace miyonos
