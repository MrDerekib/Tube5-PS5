// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_formats.hpp"
#include <cassert>
#include <cstring>
#include <string>
int main()
{
    std::array<char, 256> host;
    assert(tube5::youtube_media_host("https://rr1---sn-test.googlevideo.com/videoplayback?expire=1",
                                     host));
    assert(std::strcmp(host.data(), "rr1---sn-test.googlevideo.com") == 0);
    for (const auto *url :
         {"http://a.googlevideo.com/videoplayback?x=1",
          "https://a.googlevideo.com.evil.test/videoplayback?x=1",
          "https://a.googlevideo.com@127.0.0.1/videoplayback?x=1",
          "https://a.googlevideo.com:443/videoplayback?x=1",
          "https://.googlevideo.com/videoplayback?x=1",
          "https://a..googlevideo.com/videoplayback?x=1",
          "https://-a.googlevideo.com/videoplayback?x=1", "https://a.googlevideo.com/other?x=1",
          "https://a.googlevideo.com/videoplayback?x=1#fragment",
          "https://a.googlevideo.com/videoplayback?x=1\r\nHeader: evil"})
        assert(!tube5::youtube_media_host(url, host));
    tube5::YouTubeFormats result;
    const char *valid =
        R"({"playabilityStatus":{"status":"OK"},"videoDetails":{"title":"Test"},"streamingData":{"formats":[{"itag":18,"height":360,"mimeType":"video/mp4; codecs=\"avc1.42001E, mp4a.40.2\"","url":"https://rr1.googlevideo.com/videoplayback?x=1"}],"adaptiveFormats":[{"itag":137,"height":1080,"mimeType":"video/mp4; codecs=\"avc1.640028\"","signatureCipher":"s=x"},{"itag":251,"mimeType":"audio/webm; codecs=\"opus\"","url":"https://rr1.googlevideo.com/videoplayback?x=2"},{"itag":313,"height":2160,"mimeType":"video/webm; codecs=\"vp9\""},{"itag":399,"height":1080,"mimeType":"video/mp4; codecs=\"av01.0\""}]}})";
    assert(tube5::parse_youtube_formats(valid, result));
    assert(result.count == 5 && result.direct == 2 && result.cipher == 1);
    assert(result.max_height == 2160 && result.avc == 2 && result.aac == 1 && result.vp9 == 1 &&
           result.av1 == 1 && result.opus == 1);
    assert(result.selected_itag == 18 && result.selected_height == 360 && result.source[0]);
    assert(std::strcmp(result.title.data(), "Test") == 0);
    assert(tube5::parse_youtube_formats(
        R"({"playabilityStatus":{"status":"LOGIN_REQUIRED","reason":"Sign in"}})", result));
    assert(std::strcmp(result.status.data(), "LOGIN_REQUIRED") == 0 && !result.source[0]);
    assert(tube5::parse_youtube_formats(
        R"({"playabilityStatus":{"status":"OK"},"streamingData":{"formats":[{"itag":18,"height":360,"mimeType":"video/mp4; codecs=\"avc1.x, mp4a.x\"","url":"https://evil.test/videoplayback?x=1"}]}})",
        result));
    assert(!result.source[0]);
    assert(tube5::parse_youtube_formats(
        R"({"playabilityStatus":{"status":"OK"},"streamingData":{"adaptiveFormats":[{"itag":18,"height":360,"mimeType":"video/mp4; codecs=\"avc1.x, mp4a.x\"","url":"https://rr1.googlevideo.com/videoplayback?x=1"}]}})",
        result));
    assert(!result.source[0]);
    std::string oversized_array =
        R"({"playabilityStatus":{"status":"OK"},"streamingData":{"formats":[)";
    for (unsigned i = 0; i < 129; ++i)
        oversized_array += i ? ",{}" : "{}";
    oversized_array += "]}}";
    for (const auto &bad : {std::string{}, std::string("{}"), std::string(valid) + "garbage",
                            std::string("{\"playabilityStatus\":{\"status\":\"OK\"}"),
                            std::string(tube5::youtube_player_limit + 1, ' '), oversized_array,
                            std::string(40, '[') + "0" + std::string(40, ']')})
    {
        assert(!tube5::parse_youtube_formats(bad, result));
        assert(!result.source[0]);
    }
}
