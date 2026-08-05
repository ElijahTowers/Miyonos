#include "network/https_artwork.h"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

#ifdef MIYONOS_ONIONOS
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <curl/curl.h>
#endif

namespace miyonos {

namespace {

constexpr char kDefaultCaFile[] =
    "certificates/trusted-spotify-artwork-roots.pem";
constexpr char kHttpsPrefix[] = "https://";
constexpr char kSpotifyImageHost[] = "i.scdn.co";
constexpr char kSpotifyMosaicHost[] = "mosaic.scdn.co";
constexpr char kSpotifySeedMixHost[] = "seed-mix-image.spotifycdn.com";
constexpr char kSpotifySonosStaticHost[] = "spotify-static.ws.sonos.com";
constexpr char kSonosRadioArtworkHost[] = "sali.sonos.radio";
constexpr char kCurrentSonosRadioArtworkHost[] = "sali.sonos.superhi.fi";
constexpr char kLegacySonosRadioArtworkHost[] =
    "d1uner0r1fcap8.cloudfront.net";
constexpr char kTuneInProfilesHost[] = "cdn-profiles.tunein.com";
constexpr char kTuneInRadiotimeHost[] = "cdn-radiotime-logos.tunein.com";

enum class ArtworkEndpoint {
  None,
  Direct,
  RadioProxy,
  RadioCdn
};

bool is_hexadecimal(char character) {
  return (character >= '0' && character <= '9') ||
         (character >= 'a' && character <= 'f') ||
         (character >= 'A' && character <= 'F');
}

bool is_hexadecimal_string(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), is_hexadecimal);
}

bool is_spotify_image_path(const std::string& path) {
  constexpr char kPrefix[] = "/image/";
  if (path.rfind(kPrefix, 0) != 0) return false;
  const std::string id = path.substr(sizeof(kPrefix) - 1);
  return (id.size() == 40 || id.size() == 64) && is_hexadecimal_string(id);
}

bool is_spotify_mosaic_path(const std::string& path) {
  constexpr char kPrefix[] = "/640/";
  if (path.rfind(kPrefix, 0) != 0) return false;
  const std::string ids = path.substr(sizeof(kPrefix) - 1);
  // A Spotify 640-pixel mosaic concatenates four 40-hex cover IDs.
  return ids.size() == 160 && is_hexadecimal_string(ids);
}

bool is_safe_url_segment(const std::string& value) {
  if (value.empty() || value.size() > 160) return false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const unsigned char character = static_cast<unsigned char>(value[index]);
    if (std::isalnum(character) || character == '-' || character == '_' ||
        character == '.' || character == '~') {
      continue;
    }
    if (character != '%' || index + 2 >= value.size() ||
        !is_hexadecimal(value[index + 1]) || !is_hexadecimal(value[index + 2])) {
      return false;
    }
    index += 2;
  }
  return true;
}

bool is_spotify_seed_mix_path(const std::string& path) {
  constexpr char kPrefix[] = "/v6/img/desc/";
  if (path.rfind(kPrefix, 0) != 0) return false;
  const std::string rest = path.substr(sizeof(kPrefix) - 1);
  const std::size_t first_slash = rest.find('/');
  if (first_slash == std::string::npos) return false;
  const std::size_t second_slash = rest.find('/', first_slash + 1);
  if (second_slash == std::string::npos ||
      rest.substr(second_slash) != "/default") {
    return false;
  }
  const std::string title = rest.substr(0, first_slash);
  const std::string locale =
      rest.substr(first_slash + 1, second_slash - first_slash - 1);
  return is_safe_url_segment(title) && locale.size() == 2 &&
         std::all_of(locale.begin(), locale.end(), [](unsigned char character) {
           return character >= 'a' && character <= 'z';
         });
}

bool is_spotify_cdn_host(const std::string& host) {
  constexpr char kPrefix[] = "image-cdn-";
  constexpr char kSuffix[] = ".spotifycdn.com";
  if (host.rfind(kPrefix, 0) != 0 ||
      host.size() != sizeof(kPrefix) - 1 + 2 + sizeof(kSuffix) - 1 ||
      host.compare(host.size() - (sizeof(kSuffix) - 1), sizeof(kSuffix) - 1,
                   kSuffix) != 0) {
    return false;
  }
  const std::size_t code = sizeof(kPrefix) - 1;
  return host[code] >= 'a' && host[code] <= 'z' &&
         host[code + 1] >= 'a' && host[code + 1] <= 'z';
}

bool parse_https_url(const std::string& url, std::string* host_result,
                     std::string* path_result) {
  if (url.rfind(kHttpsPrefix, 0) != 0) return false;
  if (url.size() > 4096) return false;
  const std::size_t host_start = sizeof(kHttpsPrefix) - 1;
  const std::size_t path_start = url.find('/', host_start);
  if (path_start == std::string::npos || path_start == host_start) return false;
  const std::string host = url.substr(host_start, path_start - host_start);
  const std::string path = url.substr(path_start);
  if (host.empty() || host.find_first_of("@:#?\r\n") != std::string::npos ||
      path.empty() || path.front() != '/' ||
      path.find_first_of("\r\n#") != std::string::npos) {
    return false;
  }
  if (host_result) *host_result = host;
  if (path_result) *path_result = path;
  return true;
}

bool percent_decode(const std::string& value, std::string* result) {
  if (!result || value.size() > 3072) return false;
  result->clear();
  result->reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    const unsigned char character = static_cast<unsigned char>(value[index]);
    if (character == '%') {
      if (index + 2 >= value.size() || !is_hexadecimal(value[index + 1]) ||
          !is_hexadecimal(value[index + 2])) {
        return false;
      }
      const auto digit = [](char item) {
        return item >= '0' && item <= '9'
                   ? item - '0'
                   : item >= 'a' && item <= 'f' ? item - 'a' + 10
                                                : item - 'A' + 10;
      };
      result->push_back(static_cast<char>(digit(value[index + 1]) * 16 +
                                           digit(value[index + 2])));
      index += 2;
    } else {
      result->push_back(static_cast<char>(character));
    }
  }
  return result->find_first_of("\r\n#") == std::string::npos;
}

bool is_safe_radio_cdn_path(const std::string& path) {
  if (path.empty() || path.size() > 2048 || path.front() != '/') return false;
  return std::all_of(path.begin(), path.end(), [](unsigned char character) {
    return std::isalnum(character) || character == '/' || character == '?' ||
           character == '&' || character == '=' || character == '%' ||
           character == '-' || character == '_' || character == '.' ||
           character == '~';
  });
}

bool is_tunein_cdn_url(const std::string& url) {
  std::string host;
  std::string path;
  if (!parse_https_url(url, &host, &path)) return false;
  return (host == kTuneInProfilesHost || host == kTuneInRadiotimeHost) &&
         is_safe_radio_cdn_path(path);
}

bool is_radio_proxy_path(const std::string& path) {
  constexpr char kPrefix[] = "/image?";
  if (path.rfind(kPrefix, 0) != 0 || path.size() > 4096) return false;
  std::string width;
  std::string image;
  std::string partner;
  std::size_t start = sizeof(kPrefix) - 1;
  while (start < path.size()) {
    const std::size_t end = path.find('&', start);
    const std::string field = path.substr(start, end - start);
    const std::size_t equals = field.find('=');
    if (equals == std::string::npos || equals == 0) return false;
    const std::string key = field.substr(0, equals);
    const std::string value = field.substr(equals + 1);
    if (key == "w" && width.empty()) width = value;
    else if (key == "image" && image.empty()) image = value;
    else if (key == "partnerId" && partner.empty()) partner = value;
    else return false;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (width.empty() || image.empty() || partner != "tunein" ||
      width.size() > 4 ||
      !std::all_of(width.begin(), width.end(), [](unsigned char character) {
        return character >= '0' && character <= '9';
      })) {
    return false;
  }
  const int pixels = std::atoi(width.c_str());
  if (pixels < 1 || pixels > 1024) return false;
  std::string decoded_image;
  return percent_decode(image, &decoded_image) && is_tunein_cdn_url(decoded_image);
}

bool parse_trusted_external_artwork_url(const std::string& url,
                                        std::string* host_result,
                                        std::string* path_result,
                                        ArtworkEndpoint* endpoint_result,
                                        bool allow_radio_cdn = false) {
  std::string host;
  std::string path;
  if (!parse_https_url(url, &host, &path)) return false;
  const bool trusted =
      ((host == kSpotifyImageHost || is_spotify_cdn_host(host)) &&
       is_spotify_image_path(path)) ||
      (host == kSpotifyMosaicHost && is_spotify_mosaic_path(path)) ||
      (host == kSpotifySeedMixHost && is_spotify_seed_mix_path(path)) ||
      (host == kSpotifySonosStaticHost &&
       path == "/icons/playlist_folder_legacy.png");
  ArtworkEndpoint endpoint = trusted ? ArtworkEndpoint::Direct
      : ((host == kSonosRadioArtworkHost ||
          host == kCurrentSonosRadioArtworkHost ||
          host == kLegacySonosRadioArtworkHost) && is_radio_proxy_path(path))
            ? ArtworkEndpoint::RadioProxy
      : (allow_radio_cdn && is_tunein_cdn_url(url))
            ? ArtworkEndpoint::RadioCdn
            : ArtworkEndpoint::None;
  if (endpoint == ArtworkEndpoint::None) return false;
  if (host_result) *host_result = host;
  if (path_result) *path_result = path;
  if (endpoint_result) *endpoint_result = endpoint;
  return true;
}

std::string trim_copy(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

#ifdef MIYONOS_ONIONOS
bool parse_size(const std::string& value, std::size_t* result) {
  if (!result || value.empty()) return false;
  unsigned long long parsed = 0;
  for (const unsigned char character : value) {
    if (character < '0' || character > '9' ||
        parsed > (std::numeric_limits<unsigned long long>::max() -
                  (character - '0')) /
                     10) {
      return false;
    }
    parsed = parsed * 10 + character - '0';
  }
  if (parsed > std::numeric_limits<std::size_t>::max()) return false;
  *result = static_cast<std::size_t>(parsed);
  return true;
}
#endif

const char* ca_file() {
  const char* configured = std::getenv("MIYONOS_TLS_CA_FILE");
  return configured && *configured ? configured : kDefaultCaFile;
}

#ifdef MIYONOS_ONIONOS
HttpResponse parse_https_response(const std::string& raw,
                                  const HttpClient::Limits& limits) {
  HttpResponse response;
  const auto header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    response.error = "Incomplete HTTPS response";
    return response;
  }
  if (header_end > limits.max_header_bytes) {
    response.error = "HTTPS headers exceed limit";
    return response;
  }
  std::istringstream stream(raw.substr(0, header_end));
  std::string status_line;
  std::getline(stream, status_line);
  std::istringstream status(status_line);
  std::string version;
  status >> version >> response.status;
  std::getline(status, response.reason);
  response.reason = trim_copy(response.reason);
  if (version.rfind("HTTP/", 0) != 0 || response.status == 0) {
    response.error = "Malformed HTTPS status";
    return response;
  }
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    response.headers[lowercase(trim_copy(line.substr(0, colon)))] =
        trim_copy(line.substr(colon + 1));
  }
  const std::string encoded = raw.substr(header_end + 4);
  const auto transfer = response.headers.find("transfer-encoding");
  const bool chunked = transfer != response.headers.end() &&
      lowercase(transfer->second).find("chunked") != std::string::npos;
  if (chunked) {
    decode_chunked_body(encoded, response.body, limits.max_body_bytes,
                        response.error);
    return response;
  }
  const auto content = response.headers.find("content-length");
  if (content != response.headers.end()) {
    std::size_t size = 0;
    if (!parse_size(content->second, &size) || size > limits.max_body_bytes) {
      response.error = "Invalid or excessive Content-Length";
      return response;
    }
    if (encoded.size() < size) {
      response.error = "Truncated HTTPS body";
      return response;
    }
    response.body.assign(encoded, 0, size);
  } else {
    if (encoded.size() > limits.max_body_bytes) {
      response.error = "HTTPS body exceeds limit";
      return response;
    }
    response.body = encoded;
  }
  return response;
}
#endif

#ifndef MIYONOS_ONIONOS
struct CurlBody {
  std::string bytes;
  std::size_t maximum = 0;
  std::size_t header_bytes = 0;
  std::size_t maximum_headers = 0;
  const std::atomic<bool>* cancelled = nullptr;
  HttpResponse* response = nullptr;
  bool exceeded = false;
};

size_t curl_write(char* data, size_t size, size_t count, void* opaque) {
  CurlBody* body = static_cast<CurlBody*>(opaque);
  const std::size_t bytes = size * count;
  if (!body || (body->cancelled && body->cancelled->load()) ||
      bytes > body->maximum - std::min(body->maximum, body->bytes.size())) {
    if (body) body->exceeded = true;
    return 0;
  }
  body->bytes.append(data, bytes);
  return bytes;
}

size_t curl_header(char* data, size_t size, size_t count, void* opaque) {
  CurlBody* body = static_cast<CurlBody*>(opaque);
  const std::size_t bytes = size * count;
  if (!body || bytes > body->maximum_headers -
      std::min(body->maximum_headers, body->header_bytes)) {
    if (body) body->exceeded = true;
    return 0;
  }
  body->header_bytes += bytes;
  if (!body->response) return bytes;
  const std::string line(data, bytes);
  if (line.rfind("HTTP/", 0) == 0) {
    body->response->headers.clear();
    return bytes;
  }
  const std::size_t colon = line.find(':');
  if (colon == std::string::npos) return bytes;
  body->response->headers[lowercase(trim_copy(line.substr(0, colon)))] =
      trim_copy(line.substr(colon + 1));
  return bytes;
}

int curl_progress(void* opaque, curl_off_t, curl_off_t, curl_off_t,
                  curl_off_t) {
  const auto* cancelled = static_cast<const std::atomic<bool>*>(opaque);
  return cancelled && cancelled->load() ? 1 : 0;
}
#endif

#ifdef MIYONOS_ONIONOS
class Socket {
 public:
  explicit Socket(int fd = -1) : fd_(fd) {}
  ~Socket() { if (fd_ >= 0) close(fd_); }
  Socket(const Socket&) = delete;
  Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  Socket& operator=(Socket&& other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) close(fd_);
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }
  int get() const { return fd_; }
 private:
  int fd_;
};

bool wait_fd(int fd, bool write, int timeout_ms) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(fd, &set);
  timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  int result = 0;
  do {
    fd_set working = set;
    result = select(fd + 1, write ? nullptr : &working,
                    write ? &working : nullptr, nullptr, &timeout);
  } while (result < 0 && errno == EINTR);
  return result > 0;
}

bool public_ipv4(const sockaddr_in& address) {
  const uint32_t value = ntohl(address.sin_addr.s_addr);
  const unsigned a = (value >> 24) & 0xff;
  const unsigned b = (value >> 16) & 0xff;
  if (a == 0 || a == 10 || a == 127 || a >= 224) return false;
  if (a == 100 && b >= 64 && b <= 127) return false;
  if (a == 169 && b == 254) return false;
  if (a == 172 && b >= 16 && b <= 31) return false;
  if (a == 192 && b == 168) return false;
  if (a == 198 && (b == 18 || b == 19)) return false;
  return true;
}

bool ssl_wait(SSL* ssl, int result, int timeout_ms) {
  const int error = SSL_get_error(ssl, result);
  if (error == SSL_ERROR_WANT_READ) return wait_fd(SSL_get_fd(ssl), false, timeout_ms);
  if (error == SSL_ERROR_WANT_WRITE) return wait_fd(SSL_get_fd(ssl), true, timeout_ms);
  return false;
}

HttpResponse onion_https_get(const std::string& host, const std::string& path,
                             const HttpClient::Limits& limits,
                             const std::atomic<bool>* cancelled) {
  HttpResponse response;
  std::signal(SIGPIPE, SIG_IGN);
  if (cancelled && cancelled->load()) { response.error = "Cancelled"; return response; }
  if (host.empty() || path.empty()) {
    response.error = "Unsupported external artwork URL";
    return response;
  }
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  if (getaddrinfo(host.c_str(), "443", &hints, &addresses) != 0) {
    response.error = "Could not resolve external artwork host";
    return response;
  }
  Socket socket_fd;
  for (addrinfo* item = addresses; item; item = item->ai_next) {
    const auto* address = reinterpret_cast<const sockaddr_in*>(item->ai_addr);
    if (!address || !public_ipv4(*address)) continue;
    Socket candidate(socket(AF_INET, SOCK_STREAM, 0));
    if (candidate.get() < 0) continue;
    const int flags = fcntl(candidate.get(), F_GETFL, 0);
    fcntl(candidate.get(), F_SETFL, flags | O_NONBLOCK);
    int connected = connect(candidate.get(), item->ai_addr, item->ai_addrlen);
    if (connected < 0 && errno == EINPROGRESS && wait_fd(candidate.get(), true,
                                                           limits.connect_timeout_ms)) {
      int socket_error = 0;
      socklen_t size = sizeof(socket_error);
      getsockopt(candidate.get(), SOL_SOCKET, SO_ERROR, &socket_error, &size);
      connected = socket_error == 0 ? 0 : -1;
    }
    if (connected == 0) { socket_fd = std::move(candidate); break; }
  }
  freeaddrinfo(addresses);
  if (socket_fd.get() < 0) { response.error = "External artwork connection failed"; return response; }

  SSL_CTX* context = SSL_CTX_new(TLS_client_method());
  if (!context) { response.error = "Could not initialize TLS"; return response; }
  SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
  SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
  if (SSL_CTX_load_verify_locations(context, ca_file(), nullptr) != 1) {
    SSL_CTX_free(context); response.error = "Artwork trust certificate is unavailable"; return response;
  }
  SSL* ssl = SSL_new(context);
  if (!ssl) { SSL_CTX_free(context); response.error = "Could not initialize TLS"; return response; }
  SSL_set_fd(ssl, socket_fd.get());
  SSL_set_tlsext_host_name(ssl, host.c_str());
  SSL_set1_host(ssl, host.c_str());
  int result = SSL_connect(ssl);
  while (result != 1 && !(cancelled && cancelled->load()) &&
         ssl_wait(ssl, result, limits.connect_timeout_ms)) result = SSL_connect(ssl);
  if (result != 1) {
    const int ssl_error = SSL_get_error(ssl, result);
    const unsigned long library_error = ERR_peek_last_error();
    std::string detail = "TLS handshake failed (OpenSSL " +
        std::to_string(ssl_error) + ")";
    if (library_error != 0) {
      detail += ": ";
      detail += ERR_error_string(library_error, nullptr);
    }
    SSL_free(ssl); SSL_CTX_free(context);
    response.error = cancelled && cancelled->load() ? "Cancelled" : detail;
    return response;
  }
  const long verification = SSL_get_verify_result(ssl);
  if (verification != X509_V_OK) {
    const char* detail = X509_verify_cert_error_string(verification);
    SSL_free(ssl); SSL_CTX_free(context);
    response.error = cancelled && cancelled->load()
        ? "Cancelled"
        : std::string("External artwork TLS certificate rejected: ") +
              (detail ? detail : "unknown verification error");
    return response;
  }
  std::string request = "GET " + path +
      " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\nUser-Agent: Miyonos/" +
      MIYONOS_VERSION + "\r\nAccept: image/*\r\n\r\n";
  std::size_t sent = 0;
  while (sent < request.size() && !(cancelled && cancelled->load())) {
    result = SSL_write(ssl, request.data() + sent, request.size() - sent);
    if (result > 0) { sent += static_cast<std::size_t>(result); continue; }
    if (!ssl_wait(ssl, result, limits.read_timeout_ms)) break;
  }
  if (sent != request.size()) {
    SSL_free(ssl); SSL_CTX_free(context);
    response.error = cancelled && cancelled->load() ? "Cancelled" : "External artwork HTTPS send failed";
    return response;
  }
  std::string raw;
  char buffer[8192];
  const std::size_t maximum = limits.max_header_bytes + limits.max_body_bytes + 4;
  while (!(cancelled && cancelled->load())) {
    result = SSL_read(ssl, buffer, sizeof(buffer));
    if (result > 0) {
      if (static_cast<std::size_t>(result) > maximum - std::min(maximum, raw.size())) {
        response.error = "HTTPS response exceeds limit"; break;
      }
      raw.append(buffer, static_cast<std::size_t>(result));
      continue;
    }
    if (SSL_get_error(ssl, result) == SSL_ERROR_ZERO_RETURN) break;
    if (!ssl_wait(ssl, result, limits.read_timeout_ms)) { response.error = "External artwork HTTPS read timed out"; break; }
  }
  SSL_free(ssl);
  SSL_CTX_free(context);
  if (cancelled && cancelled->load()) { response.error = "Cancelled"; return response; }
  if (!response.error.empty()) return response;
  return parse_https_response(raw, limits);
}
#endif

#ifndef MIYONOS_ONIONOS
HttpResponse desktop_https_get(const std::string& url,
                               const HttpClient::Limits& limits,
                               const std::atomic<bool>* cancelled) {
  HttpResponse response;
  CurlBody body;
  body.maximum = limits.max_body_bytes;
  body.maximum_headers = limits.max_header_bytes;
  body.cancelled = cancelled;
  body.response = &response;
  CURL* curl = curl_easy_init();
  if (!curl) {
    response.error = "Could not initialize HTTPS";
    return response;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file());
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(limits.connect_timeout_ms));
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(limits.connect_timeout_ms +
                                     limits.read_timeout_ms));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &body);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancelled);
  const std::string user_agent = std::string("Miyonos/") + MIYONOS_VERSION;
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
  const CURLcode status = curl_easy_perform(curl);
  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(curl);
  response.status = static_cast<int>(code);
  if (status != CURLE_OK) {
    response.error = (cancelled && cancelled->load()) ? "Cancelled" :
        body.exceeded ? "HTTPS artwork exceeds limit" :
                        "External artwork HTTPS request failed";
    return response;
  }
  response.body = std::move(body.bytes);
  return response;
}
#endif

HttpResponse trusted_https_get_once(const std::string& url,
                                    const HttpClient::Limits& limits,
                                    const std::atomic<bool>* cancelled) {
  std::string host;
  std::string path;
  if (!parse_trusted_external_artwork_url(url, &host, &path, nullptr, true)) {
    HttpResponse response;
    response.error = "Unsupported external artwork URL";
    return response;
  }
#ifdef MIYONOS_ONIONOS
  return onion_https_get(host, path, limits, cancelled);
#else
  return desktop_https_get(url, limits, cancelled);
#endif
}

bool is_redirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

}  // namespace

bool is_trusted_external_artwork_url(const std::string& url) {
  ArtworkEndpoint endpoint = ArtworkEndpoint::None;
  return parse_trusted_external_artwork_url(url, nullptr, nullptr, &endpoint) &&
         endpoint != ArtworkEndpoint::RadioCdn;
}

std::string trusted_external_artwork_host(const std::string& url) {
  std::string host;
  return parse_trusted_external_artwork_url(url, &host, nullptr, nullptr)
             ? host
             : std::string{};
}

std::string trusted_external_artwork_path(const std::string& url) {
  std::string path;
  return parse_trusted_external_artwork_url(url, nullptr, &path, nullptr)
             ? path
             : std::string{};
}

HttpResponse get_trusted_external_artwork(const std::string& url,
                                          const HttpClient::Limits& limits,
                                          const std::atomic<bool>* cancelled) {
  HttpResponse response;
  ArtworkEndpoint endpoint = ArtworkEndpoint::None;
  if (!parse_trusted_external_artwork_url(url, nullptr, nullptr, &endpoint)) {
    response.error = "Unsupported external artwork URL";
    return response;
  }
  response = trusted_https_get_once(url, limits, cancelled);
  if (!is_redirect(response.status)) return response;
  const auto location = response.headers.find("location");
  ArtworkEndpoint redirect = ArtworkEndpoint::None;
  if (endpoint != ArtworkEndpoint::RadioProxy ||
      location == response.headers.end() ||
      !parse_trusted_external_artwork_url(location->second, nullptr, nullptr,
                                          &redirect, true) ||
      redirect != ArtworkEndpoint::RadioCdn) {
    response.error = "External artwork redirect is not permitted";
    return response;
  }
  response = trusted_https_get_once(location->second, limits, cancelled);
  if (is_redirect(response.status)) {
    response.error = "External artwork used too many redirects";
  }
  return response;
}

}  // namespace miyonos
