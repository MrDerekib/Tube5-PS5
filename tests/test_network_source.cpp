// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstring>
#include "network_source.hpp"
#include <SDL_timer.h>
extern "C"
{
#include <libavformat/avformat.h>
    int __wrap_avformat_open_input(AVFormatContext **, const char *, const AVInputFormat *,
                                   AVDictionary **);
    int __wrap_avformat_find_stream_info(AVFormatContext *, AVDictionary **);
}
static unsigned calls = 0;
static const char *timeout = "5000000";
static Uint64 ticks = 100;
static bool exceed_deadline = false;
static bool succeed = false;
static bool bounded_probe = false;
extern "C" int __real_avformat_find_stream_info(AVFormatContext *context, AVDictionary **)
{
    if (bounded_probe)
    {
        assert(context->probesize == 5 * 1024 * 1024);
        assert(context->max_analyze_duration == 5 * AV_TIME_BASE);
        assert(context->max_probe_packets == 2500);
    }
    else
        assert(context->probesize == 2147483647);
    return 0;
}
extern "C" Uint64 __wrap_SDL_GetTicks64()
{
    return ticks;
}
extern "C" int __real_avformat_open_input(AVFormatContext **context, const char *url,
                                          const AVInputFormat *, AVDictionary **options)
{
    ++calls;
    if (std::strncmp(url, "http://", 7) == 0)
    {
        assert(options && *options);
        assert(std::strcmp(av_dict_get(*options, "rw_timeout", nullptr, 0)->value, timeout) == 0);
        assert(std::strcmp(av_dict_get(*options, "protocol_whitelist", nullptr, 0)->value,
                           "http,tcp") == 0);
        assert(*context && (*context)->interrupt_callback.callback);
        auto callback = (*context)->interrupt_callback;
        assert(callback.callback(callback.opaque) == 0);
        if (exceed_deadline)
        {
            ticks += 8001;
            assert(callback.callback(callback.opaque) == 1);
        }
        if (succeed)
            return 0;
        avformat_free_context(*context);
        *context = nullptr; // Match real avformat_open_input's failure contract.
    }
    else
        assert(options == nullptr);
    return -5;
}
int main()
{
    {
        AVFormatContext first{}, second{}, other{};
        tube5::begin_network_session();
        tube5::prepare_custom_network(&first);
        tube5::prepare_custom_network(&second);
        tube5::finish_network_open();
        tube5::record_network_read(&other, AVERROR(EIO), 0);
        assert(!tube5::network_playback_error());
        tube5::record_network_read(&first, AVERROR(EIO), 0);
        assert(tube5::network_playback_error() == AVERROR(EIO));
        tube5::begin_network_session();
        tube5::prepare_custom_network(&first);
        tube5::prepare_custom_network(&second);
        tube5::finish_network_open();
        tube5::record_network_read(&second, AVERROR(ENOMEM), 0);
        assert(tube5::network_playback_error() == AVERROR(ENOMEM));
    }
    AVFormatContext *context = nullptr;
    tube5::begin_network_session();
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
    tube5::finish_network_open();
    assert(tube5::network_open_error()[0]);
    timeout = "5000000";
    exceed_deadline = true;
    assert(__wrap_avformat_open_input(&context, "http://127.0.0.1/test.mp4", nullptr, nullptr) ==
           -5);
    tube5::finish_network_open();
    assert(std::strstr(tube5::network_open_error(), "timed out"));
    assert(__wrap_avformat_open_input(&context, "assets/test-clip.mp4", nullptr, nullptr) == -5);
    tube5::finish_network_open();
    assert(!tube5::network_open_error()[0]);
    exceed_deadline = false;
    succeed = true;
    tube5::begin_network_session();
    assert(__wrap_avformat_open_input(&context, "http://127.0.0.1/test.mp4", nullptr, nullptr) ==
           0);
    // Match _ScanSource: it overwrites open options immediately before probing.
    context->probesize = context->max_analyze_duration = 2147483647;
    bounded_probe = true;
    assert(__wrap_avformat_find_stream_info(context, nullptr) == 0);
    AVFormatContext *local = avformat_alloc_context();
    local->probesize = 2147483647;
    bounded_probe = false;
    assert(__wrap_avformat_find_stream_info(local, nullptr) == 0);
    assert(local->probesize == 2147483647); // Untracked/local sources untouched.
    avformat_free_context(local);
    // Probe reads must not be mistaken for playback failures.
    tube5::record_network_read(context, AVERROR(EIO), 0);
    assert(tube5::network_playback_error() == 0);
    tube5::finish_network_open();
    tube5::record_network_read(context, AVERROR(EAGAIN), 0);
    tube5::record_network_read(context, AVERROR_EOF, 0);
    assert(tube5::network_playback_error() == 0);
    tube5::record_network_read(context, AVERROR_EOF, AVERROR(EIO));
    assert(tube5::network_playback_error() == AVERROR(EIO));
    tube5::cancel_network_session();
    auto callback = context->interrupt_callback;
    assert(callback.callback(callback.opaque) == 1);
    tube5::record_network_read(context, AVERROR_EXIT, 0);
    assert(tube5::network_playback_error() == AVERROR(EIO));
    avformat_free_context(context);
    tube5::begin_network_session();
    assert(tube5::network_playback_error() == 0);
}
