// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <curl/curl.h>
namespace tube5
{
CURLcode initialize_verified_https() noexcept;
int resolve_https_ipv4(const char *host, char *ip, std::size_t capacity, int &error) noexcept;
bool blocked_https_ipv4(const char *ip) noexcept;
const char *verified_https_ca() noexcept;
inline constexpr const char *youtube_stream_agent =
    "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip";
} // namespace tube5
