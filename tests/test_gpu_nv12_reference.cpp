// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstdint>
extern "C"
{
#include <libavutil/frame.h>
#include <libavutil/error.h>
    struct SwsContext;
    struct Kit_PacketBuffer;
    int __wrap_sws_scale_frame(SwsContext *, AVFrame *, const AVFrame *);
    int __real_sws_scale_frame(SwsContext *, AVFrame *, const AVFrame *)
    {
        // Already-NV12 frames must never reach the converter.
        assert(false);
        return AVERROR(EINVAL);
    }
    bool __real_Kit_WritePacketBuffer(Kit_PacketBuffer *, void *) { return true; }
    AVPixelFormat __real_Kit_FindAVPixelFormat(unsigned) { return AV_PIX_FMT_NONE; }
}
int main()
{
    AVFrame *native_copy = av_frame_alloc(), *converted = av_frame_alloc(), *queued = av_frame_alloc();
    assert(native_copy && converted && queued);
    native_copy->format = AV_PIX_FMT_NV12;
    native_copy->width = native_copy->height = 4;
    native_copy->pts = 1234;
    assert(av_frame_get_buffer(native_copy, 32) == 0);
    native_copy->data[0][0] = 81;
    native_copy->data[1][0] = 90;
    native_copy->data[1][1] = 240;
    const auto *chroma = native_copy->data[1];
    assert(__wrap_sws_scale_frame(reinterpret_cast<SwsContext *>(std::uintptr_t{1}),
                                  converted, native_copy) == 0);
    assert(converted->data[1] == chroma && converted->pts == 1234);
    // Model Kit's enqueue move, followed by decoder input cleanup. The queued
    // frame must retain both planes until the UI finishes uploading them.
    av_frame_move_ref(queued, converted);
    av_frame_free(&native_copy);
    assert(queued->data[0][0] == 81 && queued->data[1][0] == 90 && queued->data[1][1] == 240);
    assert(queued->width == 4 && queued->height == 4 && queued->format == AV_PIX_FMT_NV12);
    av_frame_free(&converted);
    av_frame_free(&queued);
}
