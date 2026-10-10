// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_sync_diagnostics.hpp"
#include "video_sync_state.hpp"
#include <atomic>
using namespace tube5::video_sync_detail;
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
#include <SDL.h>
extern "C"
{
#include <libavutil/frame.h>
    struct Kit_Decoder;
    struct Kit_PacketBuffer;
    struct Kit_Timer;
    int __real_Kit_LockVideoDecoderRaw(Kit_Decoder *, unsigned char ***, int **, SDL_Rect *);
    bool __real_Kit_BeginPacketBufferRead(Kit_PacketBuffer *, void *, int);
    void __real_Kit_FinishPacketBufferRead(Kit_PacketBuffer *);
    void __real_Kit_CancelPacketBufferRead(Kit_PacketBuffer *);
    double __real_Kit_GetTimerElapsed(const Kit_Timer *);
    int __real_av_frame_ref(AVFrame *, const AVFrame *);
}
namespace
{
// Only the main thread's raw-video call is tracked. Audio and demux use the
// same queue functions on other threads. No TLS or private struct ABI needed.
std::atomic<SDL_threadID> video_reader{0};
bool reading_video() noexcept
{
    const SDL_threadID reader = video_reader.load(std::memory_order_relaxed);
    return reader != 0 && reader == SDL_ThreadID();
}
} // namespace
extern "C" int __wrap_Kit_LockVideoDecoderRaw(Kit_Decoder *decoder, unsigned char ***data,
                                              int **pitch, SDL_Rect *area)
{
    video_reader.store(SDL_ThreadID(), std::memory_order_relaxed);
    const int result = __real_Kit_LockVideoDecoderRaw(decoder, data, pitch, area);
    video_reader.store(0, std::memory_order_relaxed);
    return result;
}
extern "C" bool __wrap_Kit_BeginPacketBufferRead(Kit_PacketBuffer *buffer, void *destination,
                                                 int timeout)
{
    const bool track = reading_video();
    const bool result = __real_Kit_BeginPacketBufferRead(buffer, destination, timeout);
    if (track && !result)
        ++empty_reads;
    return result;
}
extern "C" void __wrap_Kit_FinishPacketBufferRead(Kit_PacketBuffer *buffer)
{
    // The successful upload's unlock happens after LockVideoDecoderRaw has
    // returned. Finishes inside that call are frames discarded by sync.
    if (reading_video())
        ++drops;
    __real_Kit_FinishPacketBufferRead(buffer);
}
extern "C" void __wrap_Kit_CancelPacketBufferRead(Kit_PacketBuffer *buffer)
{
    if (reading_video())
        ++waits;
    __real_Kit_CancelPacketBufferRead(buffer);
}
extern "C" double __wrap_Kit_GetTimerElapsed(const Kit_Timer *timer)
{
    const double value = __real_Kit_GetTimerElapsed(timer);
    if (reading_video() && value >= 0.0 && value < 2147483.0)
        clock_ms = static_cast<int>(value * 1000.0);
    return value;
}
extern "C" int __wrap_av_frame_ref(AVFrame *destination, const AVFrame *source)
{
    const int result = __real_av_frame_ref(destination, source);
    const int numerator = time_num.load(), denominator = time_den.load();
    if (result == 0 && reading_video() && numerator > 0 && denominator > 0 &&
        destination->best_effort_timestamp != AV_NOPTS_VALUE)
    {
        const double milliseconds = static_cast<double>(destination->best_effort_timestamp) *
                                    numerator / denominator * 1000.0;
        if (milliseconds >= 0.0 && milliseconds < 2147483647.0)
            frame_ms = static_cast<int>(milliseconds);
    }
    return result;
}
#endif
