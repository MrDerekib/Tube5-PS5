// SPDX-License-Identifier: GPL-3.0-or-later
// Scoped to Tube5, not the standalone template examples.
#ifdef TUBE5_NETWORK_WRAP
#include <cstring>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
    int __real_avformat_open_input(AVFormatContext **, const char *, const AVInputFormat *,
                                   AVDictionary **);
}
extern "C" int __wrap_avformat_open_input(AVFormatContext **context, const char *url,
                                          const AVInputFormat *format, AVDictionary **options)
{
    if (!url || std::strncmp(url, "http://", 7) != 0)
        return __real_avformat_open_input(context, url, format, options);
    AVDictionary *local = nullptr;
    AVDictionary **settings = options ? options : &local;
    // FFmpeg defines rw_timeout in microseconds. Preserve explicit caller values.
    int result = av_dict_set(settings, "rw_timeout", "5000000", AV_DICT_DONT_OVERWRITE);
    if (result >= 0)
        result = av_dict_set(settings, "protocol_whitelist", "http,tcp", AV_DICT_DONT_OVERWRITE);
    if (result >= 0)
        result = __real_avformat_open_input(context, url, format, settings);
    av_dict_free(&local);
    return result;
}
#endif
