#include "network/http.h"

#include "network/https_artwork.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <netinet/in.h>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

namespace miyonos {

namespace {

class Socket {
 public:
  explicit Socket(int fd = -1) : fd_(fd) {}
  ~Socket() {
    if (fd_ >= 0) close(fd_);
  }
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  int get() const { return fd_; }

 private:
  int fd_;
};

std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

bool wait_fd(int fd, bool write, int timeout_ms) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(fd, &set);
  timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  int result;
  do {
    fd_set working = set;
    result = select(fd + 1, write ? nullptr : &working, write ? &working : nullptr,
                    nullptr, &tv);
  } while (result < 0 && errno == EINTR);
  return result > 0;
}

bool send_all(int fd, const std::string& data, int timeout_ms,
              const std::atomic<bool>* cancelled) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    if (cancelled && cancelled->load()) return false;
    if (!wait_fd(fd, true, timeout_ms)) return false;
#ifdef MSG_NOSIGNAL
    const auto count = send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
#else
    const auto count = send(fd, data.data() + offset, data.size() - offset, 0);
#endif
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    offset += static_cast<std::size_t>(count);
  }
  return true;
}

bool parse_size(const std::string& text, std::size_t& value, int base = 10) {
  if (text.empty()) return false;
  unsigned long long parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, parsed, base);
  if (result.ec != std::errc{} || result.ptr != end ||
      parsed > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  value = static_cast<std::size_t>(parsed);
  return true;
}

}  // namespace

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool valid_ipv4(const std::string& value) {
  in_addr address{};
  if (inet_pton(AF_INET, value.c_str(), &address) != 1) return false;
  const auto host = ntohl(address.s_addr);
  if (host == 0 || host == 0xffffffffU) return false;
  return true;
}

Url parse_http_url(const std::string& value) {
  Url result;
  if (value.size() > 2048) return result;
  const auto scheme_end = value.find("://");
  if (scheme_end == std::string::npos) return result;
  result.scheme = lowercase(value.substr(0, scheme_end));
  if (result.scheme != "http") return result;
  const auto authority_begin = scheme_end + 3;
  const auto path_begin = value.find('/', authority_begin);
  const std::string authority =
      value.substr(authority_begin, path_begin == std::string::npos
                                        ? std::string::npos
                                        : path_begin - authority_begin);
  if (authority.empty() || authority.find('@') != std::string::npos) return result;
  const auto colon = authority.rfind(':');
  result.host = colon == std::string::npos ? authority : authority.substr(0, colon);
  if (!valid_ipv4(result.host)) return result;
  if (colon != std::string::npos) {
    std::size_t parsed = 0;
    if (!parse_size(authority.substr(colon + 1), parsed) || parsed == 0 ||
        parsed > 65535) {
      return result;
    }
    result.port = static_cast<uint16_t>(parsed);
  }
  result.path = path_begin == std::string::npos ? "/" : value.substr(path_begin);
  if (result.path.empty() || result.path.front() != '/' ||
      result.path.find('\r') != std::string::npos ||
      result.path.find('\n') != std::string::npos) {
    return result;
  }
  result.valid = true;
  return result;
}

std::string resolve_url(const std::string& base, const std::string& reference) {
  if (reference.empty()) return {};
  const Url absolute = parse_http_url(reference);
  if (absolute.valid) return reference;
  const Url parent = parse_http_url(base);
  if (!parent.valid || reference.find("://") != std::string::npos ||
      reference.find('\r') != std::string::npos ||
      reference.find('\n') != std::string::npos) {
    return {};
  }
  std::ostringstream origin;
  origin << "http://" << parent.host;
  if (parent.port != 80) origin << ':' << parent.port;
  if (reference.front() == '/') return origin.str() + reference;
  const auto slash = parent.path.rfind('/');
  return origin.str() +
         (slash == std::string::npos ? "/" : parent.path.substr(0, slash + 1)) +
         reference;
}

bool decode_chunked_body(const std::string& encoded, std::string& decoded,
                         std::size_t max_size, std::string& error) {
  decoded.clear();
  std::size_t position = 0;
  while (true) {
    const auto line_end = encoded.find("\r\n", position);
    if (line_end == std::string::npos || line_end - position > 32) {
      error = "Malformed chunk size";
      return false;
    }
    std::string size_text = encoded.substr(position, line_end - position);
    const auto extension = size_text.find(';');
    if (extension != std::string::npos) size_text.resize(extension);
    std::size_t chunk_size = 0;
    if (!parse_size(trim(size_text), chunk_size, 16)) {
      error = "Invalid chunk size";
      return false;
    }
    position = line_end + 2;
    if (chunk_size == 0) return true;
    if (chunk_size > max_size - std::min(max_size, decoded.size()) ||
        position > encoded.size() || chunk_size > encoded.size() - position) {
      error = "Chunked body exceeds limit or is truncated";
      return false;
    }
    decoded.append(encoded, position, chunk_size);
    position += chunk_size;
    if (position + 2 > encoded.size() || encoded.compare(position, 2, "\r\n") != 0) {
      error = "Malformed chunk terminator";
      return false;
    }
    position += 2;
  }
}

HttpClient::HttpClient(std::atomic<bool>* cancelled) : cancelled_(cancelled) {}

bool HttpClient::cancelled() const {
  return cancelled_ && cancelled_->load();
}

HttpResponse HttpClient::get(const std::string& url, Limits limits) {
  return request("GET", url, {}, {}, {}, limits);
}

HttpResponse HttpClient::get(const std::string& url) {
  return get(url, Limits{});
}

HttpResponse HttpClient::get_artwork(const std::string& url,
                                     bool allow_external_https, Limits limits) {
  if (parse_http_url(url).valid) return get(url, limits);
  if (allow_external_https && is_trusted_external_artwork_url(url)) {
    return get_trusted_external_artwork(url, limits, cancelled_);
  }
  HttpResponse response;
  response.error = "External HTTPS artwork is disabled or unsupported";
  return response;
}

HttpResponse HttpClient::post(
    const std::string& url, const std::string& content_type,
    const std::string& body, const std::map<std::string, std::string>& headers,
    Limits limits) {
  return request("POST", url, content_type, body, headers, limits);
}

HttpResponse HttpClient::request(
    const std::string& method, const std::string& url,
    const std::string& content_type, const std::string& body,
    const std::map<std::string, std::string>& headers, Limits limits) {
  HttpResponse response;
  const Url parsed = parse_http_url(url);
  if (!parsed.valid) {
    response.error = "Invalid local HTTP URL";
    return response;
  }
  if (cancelled()) {
    response.error = "Cancelled";
    return response;
  }

  Socket socket_fd(socket(AF_INET, SOCK_STREAM, 0));
  if (socket_fd.get() < 0) {
    response.error = "Unable to create socket";
    return response;
  }
#ifdef SO_NOSIGPIPE
  int enabled = 1;
  setsockopt(socket_fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
  const int flags = fcntl(socket_fd.get(), F_GETFL, 0);
  fcntl(socket_fd.get(), F_SETFL, flags | O_NONBLOCK);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(parsed.port);
  inet_pton(AF_INET, parsed.host.c_str(), &address.sin_addr);
  int connected = connect(socket_fd.get(), reinterpret_cast<sockaddr*>(&address),
                          sizeof(address));
  if (connected < 0 && errno != EINPROGRESS) {
    response.error = "Connection failed";
    return response;
  }
  if (connected < 0) {
    if (!wait_fd(socket_fd.get(), true, limits.connect_timeout_ms)) {
      response.error = cancelled() ? "Cancelled" : "Connection timed out";
      return response;
    }
    int socket_error = 0;
    socklen_t length = sizeof(socket_error);
    getsockopt(socket_fd.get(), SOL_SOCKET, SO_ERROR, &socket_error, &length);
    if (socket_error != 0) {
      response.error = "Connection refused";
      return response;
    }
  }

  std::ostringstream request_text;
  request_text << method << ' ' << parsed.path << " HTTP/1.1\r\n"
               << "Host: " << parsed.host << ':' << parsed.port << "\r\n"
               << "Connection: close\r\n"
               << "User-Agent: Miyonos/" << MIYONOS_VERSION << "\r\n"
               << "Accept: */*\r\n";
  if (!content_type.empty()) request_text << "Content-Type: " << content_type << "\r\n";
  for (const auto& header : headers) {
    if (header.first.find_first_of("\r\n:") == std::string::npos &&
        header.second.find_first_of("\r\n") == std::string::npos) {
      request_text << header.first << ": " << header.second << "\r\n";
    }
  }
  if (method == "POST") request_text << "Content-Length: " << body.size() << "\r\n";
  request_text << "\r\n" << body;
  if (!send_all(socket_fd.get(), request_text.str(), limits.read_timeout_ms,
                cancelled_)) {
    response.error = cancelled() ? "Cancelled" : "Send timed out";
    return response;
  }

  std::string raw;
  raw.reserve(8192);
  char buffer[8192];
  std::size_t header_end = std::string::npos;
  std::size_t content_length = 0;
  bool has_content_length = false;
  bool chunked = false;
  while (!cancelled()) {
    if (!wait_fd(socket_fd.get(), false, limits.read_timeout_ms)) {
      response.error = "Read timed out";
      return response;
    }
    const auto count = recv(socket_fd.get(), buffer, sizeof(buffer), 0);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (count <= 0) break;
    raw.append(buffer, static_cast<std::size_t>(count));
    if (header_end == std::string::npos) {
      header_end = raw.find("\r\n\r\n");
      if (header_end == std::string::npos && raw.size() > limits.max_header_bytes) {
        response.error = "HTTP headers exceed limit";
        return response;
      }
      if (header_end != std::string::npos) {
        if (header_end > limits.max_header_bytes) {
          response.error = "HTTP headers exceed limit";
          return response;
        }
        std::istringstream headers_stream(raw.substr(0, header_end));
        std::string status_line;
        std::getline(headers_stream, status_line);
        std::istringstream status(status_line);
        std::string version;
        status >> version >> response.status;
        std::getline(status, response.reason);
        response.reason = trim(response.reason);
        std::string line;
        while (std::getline(headers_stream, line)) {
          if (!line.empty() && line.back() == '\r') line.pop_back();
          const auto colon = line.find(':');
          if (colon == std::string::npos) continue;
          response.headers[lowercase(trim(line.substr(0, colon)))] =
              trim(line.substr(colon + 1));
        }
        const auto content = response.headers.find("content-length");
        if (content != response.headers.end()) {
          has_content_length = parse_size(content->second, content_length);
          if (!has_content_length || content_length > limits.max_body_bytes) {
            response.error = "Invalid or excessive Content-Length";
            return response;
          }
        }
        const auto transfer = response.headers.find("transfer-encoding");
        chunked = transfer != response.headers.end() &&
                  lowercase(transfer->second).find("chunked") != std::string::npos;
      }
    }
    if (header_end != std::string::npos) {
      const auto body_bytes = raw.size() - header_end - 4;
      if (!chunked && body_bytes > limits.max_body_bytes) {
        response.error = "HTTP body exceeds limit";
        return response;
      }
      if (has_content_length && body_bytes >= content_length) break;
    }
  }
  if (cancelled()) {
    response.error = "Cancelled";
    return response;
  }
  if (header_end == std::string::npos) {
    response.error = "Incomplete HTTP response";
    return response;
  }
  const std::string encoded_body = raw.substr(header_end + 4);
  if (chunked) {
    if (!decode_chunked_body(encoded_body, response.body, limits.max_body_bytes,
                             response.error)) {
      return response;
    }
  } else if (has_content_length) {
    if (encoded_body.size() < content_length) {
      response.error = "Truncated HTTP body";
      return response;
    }
    response.body.assign(encoded_body, 0, content_length);
  } else {
    response.body = encoded_body;
  }
  return response;
}

}  // namespace miyonos
