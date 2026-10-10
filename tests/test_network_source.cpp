// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstring>
extern "C"
{
#include <libavformat/avformat.h>
    int __wrap_avformat_open_input(AVFormatContext **, const char *, const AVInputFormat *,
                                   AVDictionary **);
}
static unsigned calls = 0;
static const char *timeout = "5000000";
extern "C" int __real_avformat_open_input(AVFormatContext **, const char *url,
                                          const AVInputFormat *, AVDictionary **options)
{
    ++calls;
    if (std::strncmp(url, "http://", 7) == 0)
    {
        assert(options && *options);
        assert(std::strcmp(av_dict_get(*options, "rw_timeout", nullptr, 0)->value, timeout) == 0);
        assert(std::strcmp(av_dict_get(*options, "protocol_whitelist", nullptr, 0)->value,
                           "http,tcp") == 0);
    }
    else
        assert(options == nullptr);
    return -5;
}
int main()
{
    AVFormatContext *context = nullptr;
    assert(__wrap_avformat_open_input(&context, "assets/test-clip.mp4", nullptr, nullptr) == -5);
    assert(__wrap_avformat_open_input(&context, "http://127.0.0.1/test.mp4", nullptr, nullptr) ==
           -5);
    AVDictionary *options = nullptr;
    timeout = "1234";
    assert(av_dict_set(&options, "rw_timeout", timeout, 0) == 0);
    assert(__wrap_avformat_open_input(&context, "http://127.0.0.1/test.mp4", nullptr, &options) ==
           -5);
    assert(calls == 3);
    av_dict_free(&options);
}
