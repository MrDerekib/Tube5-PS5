// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipeline_diagnostics.hpp"
#include "seek_preroll.hpp"
#include <cassert>
#include <cerrno>
#include <cstring>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
    int __wrap_av_read_frame(AVFormatContext *, AVPacket *);
    int __wrap_avcodec_send_packet(AVCodecContext *, const AVPacket *);
    int __wrap_avcodec_receive_frame(AVCodecContext *, AVFrame *);
    int __wrap_avformat_seek_file(AVFormatContext *, int, int64_t, int64_t, int64_t, int);
}
struct SDL_Thread
{
};
struct SDL_mutex
{
};
extern "C" void __wrap_SDL_RunThread(SDL_Thread *);
extern "C" void __wrap_SDL_SYS_SetupThread(const char *);
extern "C" int __wrap_SDL_LockMutex_REAL(SDL_mutex *);
namespace
{
AVFormatContext format{};
AVCodecContext codec{};
AVPacket packet{};
AVFrame frame{};
SDL_Thread thread{};
SDL_mutex mutex{};
int read_return = 0, send_return = 0, receive_return = 0, seek_return = 0;
int receive_pts = -1;
void expect(unsigned row, const char *expected)
{
    char status[128]{};
    tube5::pipeline_diagnostic_status(row, status, sizeof(status));
    assert(std::strcmp(status, expected) == 0);
}
} // namespace
extern "C" int __real_av_read_frame(AVFormatContext *context, AVPacket *out)
{
    assert(context == &format && out == &packet);
    return read_return;
}
extern "C" int __real_avcodec_send_packet(AVCodecContext *context, const AVPacket *input)
{
    assert(context == &codec && input == &packet);
    return send_return;
}
extern "C" int __real_avcodec_receive_frame(AVCodecContext *context, AVFrame *out)
{
    assert(context == &codec && out == &frame);
    if (receive_pts >= 0)
    {
        out->best_effort_timestamp = receive_pts;
        receive_pts += 100;
    }
    return receive_return;
}
extern "C" int __real_avformat_seek_file(AVFormatContext *context, int stream, int64_t minimum,
                                         int64_t target, int64_t maximum, int flags)
{
    assert(context == &format && stream == -1 && minimum == -100 && target == 50 &&
           maximum == 100 && flags == 0);
    return seek_return;
}
extern "C" void __real_SDL_SYS_SetupThread(const char *name)
{
    assert(std::strcmp(name, "demux") == 0);
}
extern "C" int __real_SDL_LockMutex_REAL(SDL_mutex *received)
{
    assert(received == &mutex);
    return -1;
}
extern "C" void __real_SDL_RunThread(SDL_Thread *received)
{
    assert(received == &thread);
    __wrap_SDL_SYS_SetupThread("demux");
    assert(__wrap_SDL_LockMutex_REAL(&mutex) == -1);
}
int main()
{
    codec.codec_type = AVMEDIA_TYPE_VIDEO;
    // Source probing before session start must not count as worker I/O.
    assert(__wrap_av_read_frame(&format, &packet) == 0);
    expect(0, "W 0/0/0 READ 0/0 ERR 0");
    tube5::begin_pipeline_diagnostic();
    __wrap_SDL_RunThread(&thread);
    assert(__wrap_av_read_frame(&format, &packet) == 0);
    read_return = AVERROR(EINVAL);
    assert(__wrap_av_read_frame(&format, &packet) == read_return);
    expect(0, "W 1/1/1 READ 1/2 ERR -22");
    assert(__wrap_avcodec_send_packet(&codec, &packet) == 0);
    assert(__wrap_avcodec_receive_frame(&codec, &frame) == 0);
    send_return = receive_return = AVERROR(EAGAIN);
    assert(__wrap_avcodec_send_packet(&codec, &packet) == send_return);
    assert(__wrap_avcodec_receive_frame(&codec, &frame) == receive_return);
    expect(1, "VSEND 2 VDEC 1 ERR 0/0");
    send_return = receive_return = AVERROR(ENOMEM);
    assert(__wrap_avcodec_send_packet(&codec, &packet) == send_return);
    assert(__wrap_avcodec_receive_frame(&codec, &frame) == receive_return);
    expect(1, "VSEND 3 VDEC 1 ERR -12/-12");
    codec.codec_type = AVMEDIA_TYPE_AUDIO;
    assert(__wrap_avcodec_send_packet(&codec, &packet) == send_return);
    expect(1, "VSEND 3 VDEC 1 ERR -12/-12");
    seek_return = AVERROR(EINVAL);
    assert(__wrap_avformat_seek_file(&format, -1, -100, 50, 100, 0) == seek_return);
    expect(2, "LOCKFAIL 1 SEEKEXEC 1 ERR -22");
    tube5::end_pipeline_diagnostic();
    assert(__wrap_av_read_frame(&format, &packet) == read_return);
    expect(0, "W 1/1/1 READ 1/2 ERR -22");
    tube5::begin_pipeline_diagnostic();
    expect(0, "W 0/0/0 READ 0/0 ERR 0");
    expect(1, "VSEND 0 VDEC 0 ERR 0/0");
    expect(2, "LOCKFAIL 0 SEEKEXEC 0 ERR 0");
    // A successful seek decodes and unreferences preroll before returning
    // the first frame at/after the target. Failed seeks preserve the target.
    codec.codec_type = AVMEDIA_TYPE_VIDEO;
    codec.pkt_timebase = {1, 1000000};
    seek_return = receive_return = 0;
    assert(__wrap_avformat_seek_file(&format, -1, -100, 50, 100, 0) == 0);
    receive_pts = 0;
    assert(__wrap_avcodec_receive_frame(&codec, &frame) == 0);
    assert(receive_pts == 200 && frame.best_effort_timestamp == 100);
    expect(1, "VSEND 0 VDEC 1 ERR 0/0");
    seek_return = AVERROR(EINVAL);
    assert(__wrap_avformat_seek_file(&format, -1, -100, 50, 100, 0) == seek_return);
    assert(tube5::seek_preroll::target_us == 50);
    tube5::end_pipeline_diagnostic();
    assert(tube5::seek_preroll::target_us == AV_NOPTS_VALUE);
}
