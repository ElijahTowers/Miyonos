#pragma once

#include <atomic>
#include <cstddef>
#include <string>

#include "network/http.h"

namespace miyonos {

// The only external resources Miyonos permits: strictly validated public
// Spotify and Sonos Radio/TuneIn cover endpoints, plus a small fixed catalog
// of official Sonos product images. This is deliberately not a general-purpose
// HTTPS client.
bool is_trusted_external_artwork_url(const std::string& url);
std::string trusted_external_artwork_host(const std::string& url);
std::string trusted_external_artwork_path(const std::string& url);

// Returns a source URL only for a model whose exact official product image is
// listed in Miyonos. The caller must still obtain the owner's explicit opt-in
// before downloading it.
std::string official_sonos_product_image_url(const std::string& model_name,
                                             const std::string& model_number);

HttpResponse get_trusted_external_artwork(const std::string& url,
                                          const HttpClient::Limits& limits,
                                          const std::atomic<bool>* cancelled);

}  // namespace miyonos
