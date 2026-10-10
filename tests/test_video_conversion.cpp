// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_conversion.hpp"
#include <cassert>
#include <cstring>
#include <SDL_video.h>
extern "C"
{
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
    struct SwsContext
    {
        unsigned reserved;
    };
    struct Kit_PacketBuffer
    {
        unsigned reserved;
    };
    int __wrap_sws_scale_frame(SwsContext *, AVFrame *, const AVFrame *);
    bool __wrap_Kit_WritePacketBuffer(Kit_PacketBuffer *, void *);
    AVPixelFormat __wrap_Kit_FindAVPixelFormat(unsigned int);
}
namespace
{
SwsContext scaler;
Kit_PacketBuffer queue;
AVFrame input{}, output{}, audio{};
int scale_result = 0;
unsigned scale_calls = 0, writes = 0;
bool write_result = true;
unsigned format_calls = 0;
void expect(const char *value)
{
    char status[96]{};
    tube5::video_conversion_status(status, sizeof(status));
    assert(std::strcmp(status, value) == 0);
}
} // namespace
extern "C" int __real_sws_scale_frame(SwsContext *context, AVFrame *destination,
                                      const AVFrame *source)
{
    assert(context == &scaler && destination == &output && source == &input);
    ++scale_calls;
    return scale_result;
}
extern "C" bool __real_Kit_WritePacketBuffer(Kit_PacketBuffer *buffer, void *source)
{
    assert(buffer == &queue && (source == &output || source == &audio));
    ++writes;
    return write_result;
}
extern "C" AVPixelFormat __real_Kit_FindAVPixelFormat(unsigned int format)
{
    assert(format == SDL_PIXELFORMAT_RGBA32);
    ++format_calls;
    return AV_PIX_FMT_RGBA;
}
int main()
{
    assert(__wrap_Kit_FindAVPixelFormat(SDL_PIXELFORMAT_IYUV) == AV_PIX_FMT_YUV420P);
    assert(format_calls == 0);
    assert(__wrap_Kit_FindAVPixelFormat(SDL_PIXELFORMAT_RGBA32) == AV_PIX_FMT_RGBA);
    assert(format_calls == 1);
    tube5::reset_video_conversion();
    expect("YUV SCALE 0 FAIL 0 DROP 0 ERR 0");
    assert(__wrap_sws_scale_frame(&scaler, &output, &input) == 0);
    assert(__wrap_Kit_WritePacketBuffer(&queue, &output));
    assert(scale_calls == 1 && writes == 1);
    scale_result = -12;
    assert(__wrap_sws_scale_frame(&scaler, &output, &input) == -12);
    assert(__wrap_Kit_WritePacketBuffer(&queue, &audio));
    assert(!__wrap_Kit_WritePacketBuffer(&queue, &output));
    assert(writes == 2); // Failed conversion did not enter the queue.
    expect("YUV SCALE 1 FAIL 1 DROP 1 ERR -12");
    scale_result = 0;
    assert(__wrap_sws_scale_frame(&scaler, &output, &input) == 0);
    write_result = false;
    assert(!__wrap_Kit_WritePacketBuffer(&queue, &output));
    assert(writes == 3); // Preserve a real writer's failure too.
    assert(__wrap_sws_scale_frame(nullptr, &output, &input) < 0);
    assert(scale_calls == 3 && !__wrap_Kit_WritePacketBuffer(&queue, &output));
    assert(writes == 3);
    tube5::reset_video_conversion();
    write_result = true;
    assert(__wrap_Kit_WritePacketBuffer(&queue, &output));
    expect("YUV SCALE 0 FAIL 0 DROP 0 ERR 0");
}
