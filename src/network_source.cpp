// SPDX-License-Identifier: GPL-3.0-or-later
// Scoped to Tube5, not the standalone template examples.
#include "network_source.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include "boot_diagnostics.hpp"
#include <SDL_timer.h>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#ifdef TUBE5_NETWORK_WRAP
    int __real_avformat_open_input(AVFormatContext **, const char *, const AVInputFormat *,
                                   AVDictionary **);
    int __real_avformat_find_stream_info(AVFormatContext *, AVDictionary **);
#endif
}
namespace
{
// One active player; previous AVIO readers are joined before the next open.
std::atomic<Uint64> deadline{0};
std::atomic<bool> cancelled{false}, playing{false};
std::atomic<const void *> network_context{nullptr};
std::atomic<int> playback_error{0};
AVIOInterruptCB original_interrupt{};
int open_error = 0;
char error_text[128]{};
bool expired() noexcept
{
    const Uint64 end = deadline.load();
    return end && SDL_GetTicks64() >= end;
}
int interrupt_open(void *)
{
    return cancelled.load() || expired() ||
           (original_interrupt.callback && original_interrupt.callback(original_interrupt.opaque));
}
} // namespace
namespace tube5
{
void begin_network_session() noexcept
{
    cancelled = false;
    playing = false;
    network_context = nullptr;
    playback_error = 0;
    deadline = 0;
    open_error = 0;
}
void cancel_network_session() noexcept
{
    cancelled = true;
}
void record_network_read(const void *context, int result, int io_error) noexcept
{
    if (!context || !playing.load() || context != network_context.load() || cancelled.load())
        return;
    const int error = result == AVERROR_EOF ? io_error : result;
    if (error < 0 && error != AVERROR(EAGAIN) && error != AVERROR_EOF)
    {
        int expected = 0;
        playback_error.compare_exchange_strong(expected, error);
    }
}
int network_playback_error() noexcept
{
    return playback_error.load();
}
void finish_network_open() noexcept
{
    if (expired())
        open_error = AVERROR(ETIMEDOUT);
    deadline = 0;
    playing = true;
}
const char *network_open_error() noexcept
{
    error_text[0] = '\0';
    if (open_error < 0)
    {
        char detail[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(open_error, detail, sizeof(detail));
        std::snprintf(error_text, sizeof(error_text), "HTTP %d: %s", open_error, detail);
    }
    return error_text;
}
} // namespace tube5
#ifdef TUBE5_NETWORK_WRAP
extern "C" int __wrap_avformat_find_stream_info(AVFormatContext *context, AVDictionary **options)
{
    if (context && context == network_context.load())
    {
        // Kitchensink's _ScanSource overwrites options with INT_MAX just before
        // this call. Set the actual public fields here, not open_input options
        // that would be silently replaced. These are probe limits, not a total
        // heap cap and not limits on MP4 sample/index tables.
        context->probesize = 5 * 1024 * 1024;
        context->max_analyze_duration = 5 * AV_TIME_BASE;
        context->max_probe_packets = 2500;
    }
    return __real_avformat_find_stream_info(context, options);
}
extern "C" int __wrap_avformat_open_input(AVFormatContext **context, const char *url,
                                          const AVInputFormat *format, AVDictionary **options)
{
    open_error = 0;
    if (!url || std::strncmp(url, "http://", 7) != 0)
        return __real_avformat_open_input(context, url, format, options);
    tube5_boot_checkpoint("Tube5 HTTP H01: abrir conexion");
    if (!context)
        return AVERROR(EINVAL);
    const bool allocated = !*context;
    if (allocated)
        *context = avformat_alloc_context();
    if (!*context)
        return AVERROR(ENOMEM);
    original_interrupt = (*context)->interrupt_callback;
    network_context = *context;
    (*context)->interrupt_callback = {interrupt_open, nullptr};
    deadline = SDL_GetTicks64() + 8000;
    AVDictionary *local = nullptr;
    AVDictionary **settings = options ? options : &local;
    // FFmpeg defines rw_timeout in microseconds. Preserve explicit caller values.
    int result = av_dict_set(settings, "rw_timeout", "5000000", AV_DICT_DONT_OVERWRITE);
    if (result >= 0)
        result = av_dict_set(settings, "protocol_whitelist", "http,tcp", AV_DICT_DONT_OVERWRITE);
    if (result >= 0)
        result = __real_avformat_open_input(context, url, format, settings);
    else
    {
        if (allocated)
        {
            avformat_free_context(*context);
            *context = nullptr;
        }
        else
            (*context)->interrupt_callback = original_interrupt;
    }
    open_error = result;
    tube5_boot_result("Tube5 HTTP H02: apertura retorno", static_cast<unsigned>(result));
    av_dict_free(&local);
    return result;
}
#endif
