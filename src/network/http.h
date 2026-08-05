#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace miyonos {

struct Url {
  std::string scheme;
  std::string host;
  uint16_t port = 80;
  std::string path = "/";
  bool valid = false;
};

struct HttpResponse {
  int status = 0;
  std::string reason;
  std::map<std::string, std::string> headers;
  std::string body;
  std::string error;

  bool ok() const { return status >= 200 && status < 300 && error.empty(); }
};

std::string lowercase(std::string value);
bool valid_ipv4(const std::string& value);
Url parse_http_url(const std::string& value);
std::string resolve_url(const std::string& base, const std::string& reference);
bool decode_chunked_body(const std::string& encoded, std::string& decoded,
                         std::size_t max_size, std::string& error);

class HttpClient {
 public:
  struct Limits {
    int connect_timeout_ms = 1600;
    int read_timeout_ms = 2500;
    std::size_t max_header_bytes = 16 * 1024;
    std::size_t max_body_bytes = 2 * 1024 * 1024;
  };

  explicit HttpClient(std::atomic<bool>* cancelled = nullptr);
  HttpResponse get(const std::string& url, Limits limits);
  HttpResponse get(const std::string& url);
  HttpResponse get_artwork(const std::string& url, bool allow_external_https,
                           Limits limits);
  HttpResponse post(const std::string& url, const std::string& content_type,
                    const std::string& body,
                    const std::map<std::string, std::string>& headers,
                    Limits limits);

 private:
  HttpResponse request(const std::string& method, const std::string& url,
                       const std::string& content_type, const std::string& body,
                       const std::map<std::string, std::string>& headers,
                       Limits limits);
  bool cancelled() const;
  std::atomic<bool>* cancelled_;
};

}  // namespace miyonos
