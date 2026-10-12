// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual AVCC filter / player adapter with a software native-API stand-in.
#include "native_video_codec.hpp"
#include "hardware_frame_probe.hpp"
#include <cassert>
#include <cstring>
#include <vector>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
    void __real_avcodec_free_context(AVCodecContext **);
    void __real_avcodec_flush_buffers(AVCodecContext *);
}
namespace
{
AVCodecContext *reference = nullptr;
AVFrame *decoded = nullptr;
std::vector<unsigned char> pixels(2048 * 1088 * 3 / 2);
tube5::HardwareStreamStatus status{};
} // namespace
namespace tube5
{
const HardwareStreamStatus &hardware_stream_status() noexcept
{
    return status;
}
int hardware_stream_begin() noexcept
{
    status = {};
    status.active = true;
    status.stage = "READY";
    reference = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
    reference->thread_count = 1;
    decoded = av_frame_alloc();
    return avcodec_open2(reference, nullptr, nullptr);
}
int hardware_stream_send(const unsigned char *data, std::size_t size, std::uint64_t pts,
                         std::uint64_t dts) noexcept
{
    if (data)
    {
        unsigned aud = 0, slice = 0;
        for (std::size_t i = 0; i + 3 < size; ++i)
            if (!data[i] && !data[i + 1] && data[i + 2] == 1)
            {
                const auto type = data[i + 3] & 31u;
                aud += type == 9;
                slice += type == 1 || type == 5;
            }
        assert(aud == 1 && slice > 0);
    }
    AVPacket *packet = av_packet_alloc();
    if (data)
    {
        assert(!av_new_packet(packet, size));
        std::memcpy(packet->data, data, size);
        packet->pts = pts;
        packet->dts = dts;
    }
    int code = avcodec_send_packet(reference, data ? packet : nullptr);
    av_packet_free(&packet);
    if (!code)
        ++status.submitted;
    return code == AVERROR(EAGAIN) ? 2 : code == AVERROR_EOF ? 1 : code;
}
int hardware_stream_receive(VideoOutput &output, VideoPictureInfo &info) noexcept
{
    const int code = avcodec_receive_frame(reference, decoded);
    if (code)
        return code == AVERROR(EAGAIN) ? 2 : code == AVERROR_EOF ? 1 : code;
    assert(decoded->width == 1920 && decoded->height == 1080 &&
           decoded->format == AV_PIX_FMT_YUV420P);
    for (int y = 0; y < 1080; ++y)
        std::memcpy(pixels.data() + y * 2048, decoded->data[0] + y * decoded->linesize[0], 1920);
    for (int y = 0; y < 540; ++y)
        for (int x = 0; x < 960; ++x)
        {
            pixels[2048 * 1088 + y * 2048 + x * 2] = decoded->data[1][y * decoded->linesize[1] + x];
            pixels[2048 * 1088 + y * 2048 + x * 2 + 1] =
                decoded->data[2][y * decoded->linesize[2] + x];
        }
    output = {};
    output.buffer = pixels.data();
    output.width = 1920;
    output.height = 1088;
    output.pitch_bytes = 2048;
    output.bytes = pixels.size();
    info = {};
    info.pts = decoded->pts;
    info.dts = decoded->pkt_dts;
    ++status.received;
    return 0;
}
void hardware_stream_release() noexcept
{
    std::memset(pixels.data(), 0, pixels.size());
    av_frame_unref(decoded);
}
int hardware_stream_reset() noexcept
{
    __real_avcodec_flush_buffers(reference);
    ++status.resets;
    return 0;
}
int hardware_stream_close() noexcept
{
    __real_avcodec_free_context(&reference);
    av_frame_free(&decoded);
    status.active = false;
    return 0;
}
} // namespace tube5
int main(int argc, char **argv)
{
    assert(argc == 2);
    AVFormatContext *source = nullptr;
    assert(!avformat_open_input(&source, argv[1], nullptr, nullptr));
    assert(avformat_find_stream_info(source, nullptr) >= 0);
    int video = av_find_best_stream(source, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    assert(video >= 0);
    AVCodecContext *ctx = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
    assert(!avcodec_parameters_to_context(ctx, source->streams[video]->codecpar));
    ctx->pkt_timebase = source->streams[video]->time_base;
    assert(!avcodec_open2(ctx, nullptr, nullptr));
    tube5::native_codec_adopt(ctx);
    assert(!tube5::native_codec_active());
    tube5::native_codec_allow_open(true);
    tube5::native_codec_adopt(ctx);
    tube5::native_codec_allow_open(false);
    assert(tube5::native_codec_active());
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *held = av_frame_alloc();
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        unsigned frames = 0;
        int64_t last = AV_NOPTS_VALUE;
        auto receive = [&]()
        {
            int code;
            while (!(code = tube5::native_codec_receive(ctx, frame)))
            {
                assert(frame->format == AV_PIX_FMT_NV12 && frame->width == 1920 &&
                       frame->height == 1080);
                assert(last == AV_NOPTS_VALUE || frame->pts > last);
                last = frame->pts;
                assert(frame->data[0][0] != 0); // Native slot was already overwritten by release().
                if (!frames)
                    assert(!av_frame_ref(held, frame));
                ++frames;
                av_frame_unref(frame);
            }
            assert(code == AVERROR(EAGAIN) || code == AVERROR_EOF);
            return code;
        };
        while (av_read_frame(source, packet) >= 0)
        {
            if (packet->stream_index == video)
            {
                int code;
                while ((code = tube5::native_codec_send(ctx, packet)) == AVERROR(EAGAIN))
                    receive();
                assert(!code);
                receive();
            }
            av_packet_unref(packet);
        }
        assert(!tube5::native_codec_send(ctx, nullptr));
        assert(receive() == AVERROR_EOF && frames == 600);
        assert(!tube5::native_codec_error() && held->data[0][0] != 0);
        av_frame_unref(held);
        if (!cycle)
        {
            assert(avformat_seek_file(source, video, INT64_MIN, 0, INT64_MAX, 0) >= 0);
            avcodec_flush_buffers(ctx);
            assert(status.resets == 1);
        }
    }
    avcodec_free_context(&ctx);
    assert(!tube5::native_codec_active() && !status.active);
    av_packet_free(&packet);
    av_frame_free(&frame);
    av_frame_free(&held);
    avformat_close_input(&source);
}
