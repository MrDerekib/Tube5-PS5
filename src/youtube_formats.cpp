// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_formats.hpp"
#include "youtube_input.hpp"
#include <algorithm>
#include <cstring>
#include <json-c/json.h>
namespace
{
bool vp9_profile0(std::string_view mime)
{
    const auto start = mime.find("codecs=");
    if (start == std::string_view::npos)
        return false;
    auto codec = mime.substr(start + 7);
    if (!codec.empty() && codec.front() == '"')
    {
        codec.remove_prefix(1);
        const auto end = codec.find('"');
        if (end == std::string_view::npos)
            return false;
        codec = codec.substr(0, end);
    }
    const bool numeric =
        std::all_of(codec.begin(), codec.end(), [](char c)
                    { return (c >= '0' && c <= '9') || c == '.' || c == 'v' || c == 'p'; });
    return codec == "vp9" ||
           (numeric && codec.size() >= 13 && codec.starts_with("vp09.00.") &&
            codec.substr(11, 2) == "08" && codec[8] >= '0' && codec[8] <= '5' && codec[9] >= '0' &&
            codec[9] <= '9' && (codec[8] != '5' || codec[9] == '0') && codec[10] == '.' &&
            (codec.size() == 13 || codec[13] == '.'));
}
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
bool parse_youtube_formats(std::string_view json, YouTubeFormats &out,
                           unsigned requested_height) noexcept
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
    const auto video_id = string_member(member(root, "videoDetails", json_type_object), "videoId");
    if (valid_youtube_id(video_id))
        std::copy(video_id.begin(), video_id.end(), out.video_id.begin());
    auto *stream = member(root, "streamingData", json_type_object);
    out.hls = !string_member(stream, "hlsManifestUrl").empty();
    out.dash = !string_member(stream, "dashManifestUrl").empty();
    bool bounded = true;
    json_object *selected_video = nullptr, *selected_audio = nullptr;
    bool audio_default = false;
    unsigned audio_bitrate = 0;
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
            const auto fps = integer_member(format, "fps");
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
            const bool separate = std::strcmp(group, "adaptiveFormats") == 0;
            const bool eligible = status == "OK" && !separate && height && height <= 1080 &&
                                  mime.starts_with("video/mp4;") && avc && aac &&
                                  youtube_media_host(url, host);
            const bool h264_video =
                height && height <= 1080 && mime.starts_with("video/mp4;") && avc;
            const bool vp9_video =
                height == 1440 && integer_member(format, "width") == 2560 &&
                (mime.starts_with("video/webm;") || mime.starts_with("video/mp4;")) &&
                vp9_profile0(mime);
            const bool candidate = separate && fps && fps <= 30 && (h264_video || vp9_video) &&
                                   !aac &&
                                   string_member(format, "type") != "FORMAT_STREAM_TYPE_OTF";
            const bool adaptive = status == "OK" && candidate && youtube_media_host(url, host);
            if (requested_height && adaptive && height == requested_height && !selected_video)
                selected_video = format;
            if (status == "OK" && separate && !height && mime.starts_with("audio/mp4;") && aac &&
                string_member(format, "type") != "FORMAT_STREAM_TYPE_OTF" &&
                youtube_media_host(url, host))
            {
                const auto bitrate = integer_member(format, "bitrate");
                auto *track = member(format, "audioTrack", json_type_object);
                auto *is_default = member(track, "audioIsDefault", json_type_boolean);
                const bool preferred =
                    !track || (is_default && json_object_get_boolean(is_default));
                if (bitrate && bitrate <= 192000 &&
                    (!selected_audio || preferred > audio_default ||
                     (preferred == audio_default && bitrate > audio_bitrate)))
                {
                    selected_audio = format;
                    audio_default = preferred;
                    audio_bitrate = bitrate;
                }
            }
            if (height)
            {
                unsigned q = 0;
                while (q < out.quality_count && out.qualities[q].height != height)
                    ++q;
                if (q == out.quality_count)
                {
                    if (q == out.qualities.size())
                    {
                        bounded = false;
                        break;
                    }
                    out.qualities[q].height = height;
                    ++out.quality_count;
                }
                out.qualities[q].playable |= eligible;
                out.qualities[q].separate |= separate;
                out.qualities[q].candidate |= candidate;
            }
            if (eligible && (requested_height ? height == requested_height : height <= 360) &&
                height > out.selected_height)
            {
                std::copy(url.begin(), url.end(), out.source.begin());
                out.source[url.size()] = '\0';
                out.source_host = host;
                out.selected_itag = integer_member(format, "itag");
                out.selected_height = height;
            }
        }
    }
    if (!out.source[0] && selected_video && selected_audio && bounded)
    {
        const auto video_url = string_member(selected_video, "url");
        const auto audio_url = string_member(selected_audio, "url");
        std::copy(video_url.begin(), video_url.end(), out.source.begin());
        std::copy(audio_url.begin(), audio_url.end(), out.audio_source.begin());
        (void)youtube_media_host(video_url, out.source_host);
        out.selected_itag = integer_member(selected_video, "itag");
        out.selected_height = integer_member(selected_video, "height");
        out.selected_fps = integer_member(selected_video, "fps");
        const auto mime = string_member(selected_video, "mimeType");
        out.selected_webm = mime.starts_with("video/webm;");
        out.selected_vp9 = vp9_profile0(mime);
        out.audio_itag = integer_member(selected_audio, "itag");
        for (unsigned q = 0; q < out.quality_count; ++q)
            if (out.qualities[q].height == out.selected_height)
                out.qualities[q].playable = true;
    }
    std::sort(out.qualities.begin(), out.qualities.begin() + out.quality_count,
              [](const auto &a, const auto &b) { return a.height < b.height; });
    json_object_put(root);
    if (!bounded || status.empty())
    {
        out = {};
        return false;
    }
    return true;
}
} // namespace tube5
