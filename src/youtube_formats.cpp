// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_formats.hpp"
#include <algorithm>
#include <cstring>
#include <json-c/json.h>
namespace
{
json_object *member(json_object *object, const char *name, json_type type)
{
    json_object *value = nullptr;
    return object && json_object_object_get_ex(object, name, &value) &&
                   json_object_is_type(value, type)
               ? value
               : nullptr;
}
std::string_view string_member(json_object *object, const char *name)
{
    auto *value = member(object, name, json_type_string);
    return value
               ? std::string_view(json_object_get_string(value), json_object_get_string_len(value))
               : std::string_view{};
}
unsigned integer_member(json_object *object, const char *name)
{
    auto *value = member(object, name, json_type_int);
    const auto number = value ? json_object_get_int64(value) : 0;
    return number > 0 && number <= 1000000 ? static_cast<unsigned>(number) : 0;
}
template <std::size_t N> void display(std::string_view text, std::array<char, N> &out)
{
    const auto length = std::min(text.size(), N - 1);
    for (std::size_t i = 0; i < length; ++i)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        out[i] = c >= 32 && c < 127 ? static_cast<char>(c) : '?';
    }
    out[length] = '\0';
}
} // namespace
namespace tube5
{
bool youtube_media_host(std::string_view url, std::array<char, 256> &host) noexcept
{
    host = {};
    if (!url.starts_with("https://") || url.size() >= 8192)
        return false;
    url.remove_prefix(8);
    const auto end = url.find('/');
    if (end == std::string_view::npos || end == 0 || end >= host.size())
        return false;
    const std::string_view name(url.data(), end);
    const std::string_view path(url.data() + end, url.size() - end);
    if (!name.ends_with(".googlevideo.com") || !path.starts_with("/videoplayback?"))
        return false;
    unsigned label = 0;
    char previous = 0;
    for (const char c : name)
    {
        if (c == '.')
        {
            if (!label || previous == '-')
                return false;
            label = 0;
        }
        else
        {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') ||
                (!label && c == '-') || ++label > 63)
                return false;
        }
        previous = c;
    }
    if (!label || previous == '-')
        return false;
    for (unsigned char c : path)
        if (c <= 32 || c >= 127 || c == '#' || c == '\\')
            return false;
    std::copy(name.begin(), name.end(), host.begin());
    return true;
}
bool parse_youtube_formats(std::string_view json, YouTubeFormats &out) noexcept
{
    out = {};
    if (json.empty() || json.size() > youtube_player_limit)
        return false;
    auto *tokener = json_tokener_new_ex(32);
    if (!tokener)
        return false;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    auto *root = json_tokener_parse_ex(tokener, json.data(), static_cast<int>(json.size()));
    const bool valid = json_tokener_get_error(tokener) == json_tokener_success &&
                       json_tokener_get_parse_end(tokener) == json.size() && root &&
                       json_object_is_type(root, json_type_object);
    json_tokener_free(tokener);
    if (!valid)
    {
        if (root)
            json_object_put(root);
        return false;
    }
    auto *playability = member(root, "playabilityStatus", json_type_object);
    const auto status = string_member(playability, "status");
    display(status, out.status);
    display(string_member(playability, "reason"), out.reason);
    display(string_member(member(root, "videoDetails", json_type_object), "title"), out.title);
    auto *stream = member(root, "streamingData", json_type_object);
    out.hls = !string_member(stream, "hlsManifestUrl").empty();
    out.dash = !string_member(stream, "dashManifestUrl").empty();
    bool bounded = true;
    for (const char *group : {"formats", "adaptiveFormats"})
    {
        auto *formats = member(stream, group, json_type_array);
        const auto length = formats ? json_object_array_length(formats) : 0;
        if (length > 128 || out.count + length > 128)
        {
            bounded = false;
            break;
        }
        for (std::size_t i = 0; i < length; ++i)
        {
            auto *format = json_object_array_get_idx(formats, i);
            if (!json_object_is_type(format, json_type_object))
                continue;
            ++out.count;
            const auto mime = string_member(format, "mimeType");
            const auto url = string_member(format, "url");
            out.direct += !url.empty();
            out.cipher += !string_member(format, "signatureCipher").empty() ||
                          !string_member(format, "cipher").empty();
            const auto height = integer_member(format, "height");
            out.max_height = std::max(out.max_height, height);
            const bool avc = mime.find("avc1.") != std::string_view::npos;
            const bool aac = mime.find("mp4a.") != std::string_view::npos;
            out.avc += avc;
            out.aac += aac;
            out.vp9 += mime.find("vp9") != std::string_view::npos ||
                       mime.find("vp09") != std::string_view::npos;
            out.av1 += mime.find("av01") != std::string_view::npos;
            out.opus += mime.find("opus") != std::string_view::npos;
            std::array<char, 256> host{};
            if (status == "OK" && std::strcmp(group, "formats") == 0 && height && height <= 360 &&
                height > out.selected_height && mime.starts_with("video/mp4;") && avc && aac &&
                youtube_media_host(url, host))
            {
                std::copy(url.begin(), url.end(), out.source.begin());
                out.source[url.size()] = '\0';
                out.source_host = host;
                out.selected_itag = integer_member(format, "itag");
                out.selected_height = height;
            }
        }
    }
    json_object_put(root);
    if (!bounded || status.empty())
    {
        out = {};
        return false;
    }
    return true;
}
} // namespace tube5
