// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipeline_diagnostics.hpp"
#include "video_sync_diagnostics.hpp"
#include "video_conversion.hpp"
#include "network_source.hpp"
#include <atomic>
#include <cstdio>

namespace
{
static_assert(std::atomic<unsigned>::is_always_lock_free);
std::atomic<bool> active{false};
std::atomic<unsigned> entered{0}, ready{0}, exited{0}, reads{0}, packets{0}, sends{0}, decoded{0},
    lock_failures{0}, seeks{0};
std::atomic<int> read_result{0}, send_error{0}, decode_error{0}, seek_result{0};
} // namespace
namespace tube5
{
void begin_pipeline_diagnostic() noexcept
{
    active = false;
    entered = ready = exited = reads = packets = sends = decoded = lock_failures = seeks = 0;
    read_result = send_error = decode_error = seek_result = 0;
    reset_video_sync_diagnostic();
    reset_video_conversion();
    active = true;
}
void end_pipeline_diagnostic() noexcept
{
    active = false;
}
void pipeline_diagnostic_status(unsigned row, char *text, std::size_t capacity) noexcept
{
    if (row == 0)
        std::snprintf(text, capacity, "W %u/%u/%u READ %u/%u ERR %d", entered.load(), ready.load(),
                      exited.load(), packets.load(), reads.load(), read_result.load());
    else if (row == 1)
        std::snprintf(text, capacity, "VSEND %u VDEC %u ERR %d/%d", sends.load(), decoded.load(),
                      send_error.load(), decode_error.load());
    else
        std::snprintf(text, capacity, "LOCKFAIL %u SEEKEXEC %u ERR %d", lock_failures.load(),
                      seeks.load(), seek_result.load());
}
} // namespace tube5

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#if defined(TUBE5_NETWORK_WRAP) ||                                                                 \
    (defined(TUBE5_DIAGNOSTICS) && (!defined(TUBE5_HOST) || defined(TUBE5_PIPELINE_WRAP)))
extern "C"
{
    int __real_av_read_frame(AVFormatContext *, AVPacket *);
}
extern "C" int __wrap_av_read_frame(AVFormatContext *context, AVPacket *packet)
{
    const bool track = active.load();
    if (track)
        ++reads;
    const int result = __real_av_read_frame(context, packet);
#ifdef TUBE5_NETWORK_WRAP
    tube5::record_network_read(context, result, context && context->pb ? context->pb->error : 0);
#endif
    if (track)
    {
        read_result = result;
        if (result >= 0)
        {
            ++packets;
            if (packet->stream_index >= 0 &&
                static_cast<unsigned>(packet->stream_index) < context->nb_streams)
            {
                const AVStream *stream = context->streams[packet->stream_index];
                if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                    tube5::video_sync_time_base(stream->time_base.num, stream->time_base.den);
            }
        }
    }
    return result;
}
#endif
#if defined(TUBE5_DIAGNOSTICS) && (!defined(TUBE5_HOST) || defined(TUBE5_PIPELINE_WRAP))
extern "C"
{
    int __real_avcodec_send_packet(AVCodecContext *, const AVPacket *);
    int __real_avcodec_receive_frame(AVCodecContext *, AVFrame *);
    int __real_avformat_seek_file(AVFormatContext *, int, int64_t, int64_t, int64_t, int);
}
extern "C" int __wrap_avcodec_send_packet(AVCodecContext *context, const AVPacket *packet)
{
    const bool track = active.load() && context && context->codec_type == AVMEDIA_TYPE_VIDEO;
    if (track)
        ++sends;
    const int result = __real_avcodec_send_packet(context, packet);
    if (track && result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF)
        send_error = result;
    return result;
}
extern "C" int __wrap_avcodec_receive_frame(AVCodecContext *context, AVFrame *frame)
{
    const bool track = active.load() && context && context->codec_type == AVMEDIA_TYPE_VIDEO;
    const int result = __real_avcodec_receive_frame(context, frame);
    if (track)
    {
        if (result >= 0)
            ++decoded;
        else if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
            decode_error = result;
    }
    return result;
}
extern "C" int __wrap_avformat_seek_file(AVFormatContext *context, int stream, int64_t minimum,
                                         int64_t target, int64_t maximum, int flags)
{
    const bool track = active.load();
    if (track)
        ++seeks;
    const int result = __real_avformat_seek_file(context, stream, minimum, target, maximum, flags);
    if (track)
        seek_result = result;
    return result;
}
#endif

#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
// These internal SDL symbols are verified in the pinned PS5 archive. Desktop
// SDL hides them, so host integration wraps only the public FFmpeg functions.
struct SDL_Thread;
struct SDL_mutex;
extern "C" void __real_SDL_RunThread(SDL_Thread *);
extern "C" void __real_SDL_SYS_SetupThread(const char *);
extern "C" int __real_SDL_LockMutex_REAL(SDL_mutex *);
extern "C" void __wrap_SDL_RunThread(SDL_Thread *thread)
{
    const bool track = active.load();
    if (track)
        ++entered;
    __real_SDL_RunThread(thread);
    if (track)
        ++exited;
}
extern "C" void __wrap_SDL_SYS_SetupThread(const char *name)
{
    const bool track = active.load();
    __real_SDL_SYS_SetupThread(name);
    if (track)
        ++ready;
}
extern "C" int __wrap_SDL_LockMutex_REAL(SDL_mutex *mutex)
{
    const bool track = active.load();
    const int result = __real_SDL_LockMutex_REAL(mutex);
    if (track && result < 0)
        ++lock_failures;
    return result;
}
#endif
