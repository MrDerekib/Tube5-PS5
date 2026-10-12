// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_formats.hpp"
#include "youtube_container.hpp"
#include <cassert>
#include <string>
std::string fixture(const char *codec, const char *container = "webm", unsigned fps = 30,
                    unsigned height = 1440, unsigned width = 2560)
{
    return std::string(
               R"({"playabilityStatus":{"status":"OK"},"streamingData":{"adaptiveFormats":[{"itag":271,"height":)") +
           std::to_string(height) + ",\"width\":" + std::to_string(width) +
           ",\"fps\":" + std::to_string(fps) + ",\"mimeType\":\"video/" + container +
           "; codecs=\\\"" + codec +
           R"(\"","url":"https://rr1.googlevideo.com/videoplayback?video=1"},{"itag":140,"bitrate":128000,"mimeType":"audio/mp4; codecs=mp4a.40.2","url":"https://rr1.googlevideo.com/videoplayback?audio=1"}]}})";
}
int main()
{
    tube5::YouTubeFormats formats;
    assert(tube5::parse_youtube_formats(fixture("vp9"), formats, 1440));
    assert(formats.source[0] && formats.audio_source[0] && formats.selected_height == 1440 &&
           formats.selected_vp9 && formats.selected_webm && formats.selected_fps == 30);
    assert(tube5::parse_youtube_formats(fixture("vp09.00.50.08", "mp4"), formats, 1440));
    assert(formats.source[0] && formats.selected_vp9 && !formats.selected_webm);
    for (const auto *codec : {"vp9.2", "vp09.02.50.10", "vp09.00.50.10", "vp09.00.51.08",
                              "vp09.00.50.08evil", "av01.0"})
    {
        assert(tube5::parse_youtube_formats(fixture(codec), formats, 1440));
        assert(!formats.source[0]);
    }
    for (const auto &json : {fixture("vp9", "webm", 60), fixture("vp9", "webm", 30, 2160, 3840),
                             fixture("vp9", "webm", 30, 1440, 1920)})
    {
        assert(tube5::parse_youtube_formats(json, formats, 1440));
        assert(!formats.source[0]);
    }
    assert(tube5::parse_youtube_formats(fixture("vp9"), formats));
    assert(!formats.source[0]); // Auto remains <=360p.
    const unsigned char webm[]{0x1a, 0x45, 0xdf, 0xa3};
    const unsigned char mp4[]{0, 0, 0, 24, 'f', 't', 'y', 'p'};
    assert(tube5::youtube_container(webm, 4) == tube5::YouTubeContainer::webm);
    assert(tube5::youtube_container(mp4, 8) == tube5::YouTubeContainer::mp4);
    assert(tube5::youtube_container(webm, 3) == tube5::YouTubeContainer::unknown);
    assert(tube5::youtube_container(mp4, 7) == tube5::YouTubeContainer::unknown);
    assert(tube5::youtube_container(nullptr, 0) == tube5::YouTubeContainer::unknown);
}
