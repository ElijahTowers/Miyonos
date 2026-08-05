#pragma once

#include <atomic>
#include <cstddef>
#include <string>

#include "network/http.h"

namespace miyonos {

// The only external resources Miyonos permits: strictly validated public
// Spotify and Sonos Radio/TuneIn cover endpoints. This is deliberately not a
// general-purpose HTTPS client.
bool is_trusted_external_artwork_url(const std::string& url);
std::string trusted_external_artwork_host(const std::string& url);
std::string trusted_external_artwork_path(const std::string& url);

HttpResponse get_trusted_external_artwork(const std::string& url,
                                          const HttpClient::Limits& limits,
                                          const std::atomic<bool>* cancelled);

}  // namespace miyonos
