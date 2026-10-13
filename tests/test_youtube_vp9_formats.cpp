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
    for (const auto *codec : {"vp9", "vp09.00.51.08"})
    {
        assert(tube5::parse_youtube_formats(fixture(codec, "webm", 30, 2160, 3840), formats, 2160));
        assert(formats.source[0] && formats.audio_source[0] && formats.selected_height == 2160 &&
               formats.selected_vp9 && formats.selected_webm);
    }
    for (const auto *codec :
         {"vp9.2", "vp09.02.51.10", "vp09.00.51.10", "vp09.00.52.08",
          "vp09.00.51.08.01.09.16.09.00", "vp09.00.51.08.01.09.18.09.00", "av01.0"})
    {
        assert(tube5::parse_youtube_formats(fixture(codec, "webm", 30, 2160, 3840), formats, 2160));
        assert(!formats.source[0]);
    }
    for (const auto &json :
         {fixture("vp9", "webm", 60, 2160, 3840), fixture("vp9", "webm", 30, 2160, 2560),
          fixture("vp9", "webm", 30, 4320, 7680)})
    {
        assert(tube5::parse_youtube_formats(json, formats, 2160));
        assert(!formats.source[0]);
    }
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
    for (const auto *container : {"webm", "mp4"})
    {
        const auto json = fixture("vp09.00.51.08", container, 60, 2160, 3840);
        assert(tube5::parse_youtube_formats(json, formats, 2160, true));
        assert(formats.selected_fps == 60 && formats.source[0] && formats.audio_source[0]);
        assert(formats.qualities[0].fps == 60);
        assert(tube5::youtube_vp9_pipeline_requested(formats));
        assert(tube5::parse_youtube_formats(json, formats, 2160));
        assert(!formats.source[0] && !tube5::youtube_vp9_pipeline_requested(formats));
        assert(tube5::parse_youtube_formats(json, formats, 0, true));
        assert(!formats.source[0]); // Opt-in never changes auto/default quality.
    }
    for (const auto &json : {fixture("vp9.2", "webm", 60, 2160, 3840),
                             fixture("av01.0", "mp4", 60, 2160, 3840),
                             fixture("vp9", "webm", 120, 2160, 3840),
                             fixture("vp9", "webm", 60),
                             fixture("avc1.640028", "mp4", 60, 2160, 3840)})
    {
        assert(tube5::parse_youtube_formats(json, formats, 2160, true));
        assert(!formats.source[0]);
    }
    std::string mixed = fixture("vp9", "webm", 60, 2160, 3840);
    mixed.insert(mixed.find('[') + 1,
                 R"({"itag":313,"height":2160,"width":3840,"fps":30,"mimeType":"video/webm; codecs=vp9","url":"https://rr1.googlevideo.com/videoplayback?video=30"},)");
    assert(tube5::parse_youtube_formats(mixed, formats, 2160, true));
    assert(formats.selected_fps == 60 && tube5::youtube_vp9_pipeline_requested(formats));
    assert(tube5::parse_youtube_formats(mixed, formats, 2160));
    assert(formats.selected_fps == 30 && !tube5::youtube_vp9_pipeline_requested(formats));
    const unsigned char webm[]{0x1a, 0x45, 0xdf, 0xa3};
    const unsigned char mp4[]{0, 0, 0, 24, 'f', 't', 'y', 'p'};
    assert(tube5::youtube_container(webm, 4) == tube5::YouTubeContainer::webm);
    assert(tube5::youtube_container(mp4, 8) == tube5::YouTubeContainer::mp4);
    assert(tube5::youtube_container(webm, 3) == tube5::YouTubeContainer::unknown);
    assert(tube5::youtube_container(mp4, 7) == tube5::YouTubeContainer::unknown);
    assert(tube5::youtube_container(nullptr, 0) == tube5::YouTubeContainer::unknown);
}
