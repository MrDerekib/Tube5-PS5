// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>
namespace tube5
{
constexpr unsigned youtube_player_limit = 1024 * 1024;
struct YouTubeFormats
{
    std::array<char, 128> title{};
    std::array<char, 48> status{};
    std::array<char, 128> reason{};
    // Ephemeral signed URL: never log, persist or include in UI screenshots.
    std::array<char, 8192> source{};
    std::array<char, 256> source_host{};
    unsigned count = 0, direct = 0, cipher = 0, max_height = 0;
    unsigned avc = 0, vp9 = 0, av1 = 0, aac = 0, opus = 0;
    unsigned selected_itag = 0, selected_height = 0;
    bool hls = false, dash = false;
};
bool youtube_media_host(std::string_view url, std::array<char, 256> &host) noexcept;
// Strict, depth- and body-bounded JSON. Only combined AVC/AAC MP4 <=360p
// is eligible for the initial byte probe; adaptive formats are inventory only.
bool parse_youtube_formats(std::string_view json, YouTubeFormats &out) noexcept;
} // namespace tube5
