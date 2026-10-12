// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_conversion.hpp"
#include <atomic>
#include <cstdio>

namespace
{
// There is one video converter per active Tube5 player. Audio queues use
// different frame objects. Reset only before workers start, after prior joins.
std::atomic<const void *> failed_frame{nullptr};
std::atomic<unsigned> converted{0}, conversion_failures{0}, rejected{0};
std::atomic<unsigned> nv12_references{0};
std::atomic<int> last_error{0};
} // namespace
namespace tube5
{
void reset_video_conversion() noexcept
{
    failed_frame = nullptr;
    converted = conversion_failures = rejected = 0;
    nv12_references = 0;
    last_error = 0;
}
void video_conversion_status(char *text, std::size_t capacity) noexcept
{
    std::snprintf(text, capacity, "YUV SCALE %u NV12 REF %u FAIL %u DROP %u ERR %d",
                  converted.load(), nv12_references.load(), conversion_failures.load(),
                  rejected.load(), last_error.load());
}
} // namespace tube5

#if defined(TUBE5_CONVERSION_WRAP)
#include <SDL_video.h>
extern "C"
{
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
    struct SwsContext;
    struct Kit_PacketBuffer;
    int __real_sws_scale_frame(SwsContext *, AVFrame *, const AVFrame *);
    bool __real_Kit_WritePacketBuffer(Kit_PacketBuffer *, void *);
    AVPixelFormat __real_Kit_FindAVPixelFormat(unsigned int);
}
extern "C" AVPixelFormat __wrap_Kit_FindAVPixelFormat(unsigned int format)
{
    // a2 recognizes YV12 but omitted SDL's Y/U/V alias. Raw AVFrames carry
    // Y/U/V planes, which SDL_UpdateYUVTexture uploads into an IYUV texture.
    if (format == SDL_PIXELFORMAT_IYUV)
        return AV_PIX_FMT_YUV420P;
#ifdef TUBE5_GPU_PLAYER
    if (format == SDL_PIXELFORMAT_NV12)
        return AV_PIX_FMT_NV12;
#endif
    return __real_Kit_FindAVPixelFormat(format);
}
extern "C" int __wrap_sws_scale_frame(SwsContext *context, AVFrame *destination,
                                      const AVFrame *source)
{
    int result = AVERROR(EINVAL);
#ifdef TUBE5_GPU_PLAYER
    if (context && destination && source && source->format == AV_PIX_FMT_NV12 && source->data[0] &&
        source->data[1] && source->width > 0 && source->height > 0)
    {
        // Tube5's GPU sink requests NV12 at the source dimensions. The frame
        // already owns copied native pixels: move a reference through the Kit
        // queue, preserving plane addresses, stride and lifetime without sws.
        av_frame_unref(destination);
        result = av_frame_ref(destination, source);
        if (result >= 0)
            ++nv12_references;
    }
    else
#endif
        if (context && destination && source)
        result = __real_sws_scale_frame(context, destination, source);
    if (result < 0)
    {
        failed_frame = destination;
        ++conversion_failures;
        last_error = result;
    }
    else
    {
        failed_frame = nullptr;
        ++converted;
    }
    return result;
}
extern "C" bool __wrap_Kit_WritePacketBuffer(Kit_PacketBuffer *buffer, void *source)
{
    // Kitchensink a2 ignores sws_scale_frame's result and queues its output
    // anyway. A false write makes its existing cleanup unref the failed frame,
    // instead of handing missing/partial buffers to the raw-frame consumer.
    if (source && failed_frame.load() == source)
    {
        failed_frame = nullptr;
        ++rejected;
        return false;
    }
    return __real_Kit_WritePacketBuffer(buffer, source);
}
#endif
