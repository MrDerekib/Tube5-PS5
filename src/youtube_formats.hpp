// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>
namespace tube5
{
constexpr unsigned youtube_player_limit = 1024 * 1024;
struct YouTubeQuality
{
    unsigned height = 0;
    bool playable = false, separate = false, candidate = false;
    unsigned fps = 0;
};
struct YouTubeFormats
{
    std::array<char, 12> video_id{};
    std::array<YouTubeQuality, 16> qualities{};
    unsigned quality_count = 0;
    std::array<char, 128> title{};
    std::array<char, 48> status{};
    std::array<char, 128> reason{};
    // Ephemeral signed URL: never log, persist or include in UI screenshots.
    std::array<char, 8192> source{};
    std::array<char, 256> source_host{};
    std::array<char, 8192> audio_source{};
    unsigned audio_itag = 0, selected_fps = 0;
    unsigned count = 0, direct = 0, cipher = 0, max_height = 0;
    unsigned avc = 0, vp9 = 0, av1 = 0, aac = 0, opus = 0;
    unsigned selected_itag = 0, selected_height = 0;
    bool hls = false, dash = false;
    bool selected_webm = false, selected_vp9 = false;
};
bool youtube_media_host(std::string_view url, std::array<char, 256> &host) noexcept;
// Auto chooses <=360p. Explicit height may select combined AVC/AAC <=1080p;
// Explicit height can select separate H.264 <=1080p/30fps + default AAC.
// 1440p/2160p are VP9 Profile 0 SDR + AAC; 2160p60 is an explicit test opt-in.
bool parse_youtube_formats(std::string_view json, YouTubeFormats &out,
                           unsigned requested_height = 0, bool allow_4k60 = false) noexcept;
inline bool youtube_vp9_pipeline_requested(const YouTubeFormats &formats) noexcept
{
    return formats.selected_vp9 && formats.selected_height == 2160 &&
           formats.selected_fps > 30 && formats.selected_fps <= 60 &&
           formats.source[0] && formats.audio_source[0];
}
} // namespace tube5
