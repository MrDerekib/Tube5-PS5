// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_video_codec.hpp"
#ifdef TUBE5_HW_PLAYER
#include "hardware_frame_probe.hpp"
#include "failure_log.hpp"
#include <SDL.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/error.h>
#include <libavutil/version.h>
    void __real_avcodec_flush_buffers(AVCodecContext *);
    void __real_avcodec_free_context(AVCodecContext **);
}
namespace
{
std::atomic<bool> allow_open{false};
std::atomic<AVCodecContext *> owner{nullptr};
std::atomic<int> fatal{0};
std::atomic<unsigned> reset_generation{0};
std::atomic<unsigned> audio_reset_generation{0};
SDL_mutex *mutex = nullptr;
AVBSFContext *bsf = nullptr;
AVPacket *input = nullptr, *filtered = nullptr;
AVFrame *pending = nullptr;
bool have_frame = false, held_packet = false, draining = false, drain_sent = false, eof = false;
bool need_key = true, have_pts = false;
std::int64_t last_pts = 0;
unsigned delivered = 0, eof_events = 0, skipped = 0;
struct Lock
{
    Lock()
    {
        SDL_LockMutex(mutex);
    }
    ~Lock()
    {
        SDL_UnlockMutex(mutex);
    }
};
void snapshot(const char *stage)
{
    const auto &s = tube5::hardware_stream_status();
    char text[768]{};
    const int length = std::snprintf(
        text, sizeof(text),
        "TUBE5 HARDWARE PLAYER V1\nBUILD %s\nPLAYER HARDWARE H264 1080P - NV12 TO IYUV\n"
        "STAGE %s NATIVE STAGE %s ACTIVE %u ERROR %08X NATIVE ERROR %08X CLEANUP %08X\n"
        "SUBMITTED %u RECEIVED %u DELIVERED %u RESETS %u EOF EVENTS %u SKIPPED PREROLL %u\n"
        "LAST PTS %lld\nACCEPTED %u NO OUTPUT %u LAST AU %zu NAL MASK %08X INPUT PTS %lld DTS "
        "%lld\n",
        tube5::failure_log_build_label(), stage, s.stage, s.active ? 1u : 0u,
        static_cast<unsigned>(fatal.load()), static_cast<unsigned>(s.error),
        static_cast<unsigned>(s.cleanup_error), s.submitted, s.received, delivered, s.resets,
        eof_events, skipped, static_cast<long long>(last_pts), s.accepted, s.no_output,
        s.last_au_bytes, s.last_nal_mask, static_cast<long long>(s.last_input_pts),
        static_cast<long long>(s.last_input_dts));
    if (length > 0)
        (void)tube5::write_failure_log("/download0/tube5-hardware-player.log", text,
                                       std::min<std::size_t>(length, sizeof(text) - 1));
}
int failure(const char *stage, int code)
{
    fatal = code;
    snapshot(stage);
    return AVERROR(EIO);
}
void clear_filter_state()
{
    av_packet_unref(input);
    av_packet_unref(filtered);
    av_frame_unref(pending);
    have_frame = held_packet = draining = drain_sent = eof = have_pts = false;
    need_key = true;
}
void free_filter()
{
    av_packet_free(&input);
    av_packet_free(&filtered);
    av_frame_free(&pending);
    av_bsf_free(&bsf);
}
int collect()
{
    if (have_frame)
        return 0;
    tube5::VideoOutput output{};
    tube5::VideoPictureInfo info{};
    const int code = tube5::hardware_stream_receive(output, info);
    if (code == 2)
        return AVERROR(EAGAIN);
    if (code == 1)
    {
        if (!eof)
            ++eof_events;
        eof = true;
        snapshot("EOF");
        return AVERROR_EOF;
    }
    if (code)
        return failure("RECEIVE", code);
    const auto pts = static_cast<std::int64_t>(info.pts);
    if (pts == AV_NOPTS_VALUE || (have_pts && pts < last_pts))
    {
        tube5::hardware_stream_release();
        return failure("PTS ORDER", AVERROR_INVALIDDATA);
    }
    av_frame_unref(pending);
    pending->format = AV_PIX_FMT_NV12;
    pending->width = 1920;
    pending->height = 1080;
    const int allocation = av_frame_get_buffer(pending, 32);
    if (allocation < 0)
    {
        tube5::hardware_stream_release();
        return failure("FRAME ALLOCATION", allocation);
    }
    const auto *source = static_cast<const unsigned char *>(output.buffer);
    for (unsigned y = 0; y < 1080; ++y)
        std::memcpy(pending->data[0] + static_cast<std::size_t>(y) * pending->linesize[0],
                    source + static_cast<std::size_t>(y) * output.pitch_bytes, 1920);
    source += static_cast<std::size_t>(output.pitch_bytes) * output.height;
    for (unsigned y = 0; y < 540; ++y)
        std::memcpy(pending->data[1] + static_cast<std::size_t>(y) * pending->linesize[1],
                    source + static_cast<std::size_t>(y) * output.pitch_bytes, 1920);
    tube5::hardware_stream_release();
    const auto *context = owner.load();
    pending->pts = pending->best_effort_timestamp = pts;
    pending->pkt_dts = static_cast<std::int64_t>(info.dts);
    pending->sample_aspect_ratio = context->sample_aspect_ratio;
    pending->color_range = context->color_range;
    pending->colorspace = context->colorspace;
    pending->color_primaries = context->color_primaries;
    pending->color_trc = context->color_trc;
    last_pts = pts;
    have_pts = have_frame = true;
    return 0;
}
int pump()
{
    for (unsigned count = 0; count < 8; ++count)
    {
        if (have_frame)
            return 0;
        if (!held_packet && !drain_sent)
        {
            const int code = av_bsf_receive_packet(bsf, filtered);
            if (code == AVERROR(EAGAIN))
                return 0;
            if (code == AVERROR_EOF)
            {
                const int sent = tube5::hardware_stream_send(nullptr, 0, 0, 0);
                if (sent == 2)
                    return collect();
                if (sent)
                    return failure("DRAIN SEND", sent);
                drain_sent = true;
            }
            else if (code < 0)
                return failure("ANNEX B FILTER", code);
            else
                held_packet = true;
        }
        if (held_packet)
        {
            const int code = tube5::hardware_stream_send(filtered->data, filtered->size,
                                                         static_cast<std::uint64_t>(filtered->pts),
                                                         static_cast<std::uint64_t>(filtered->dts));
            if (code == 2)
                return collect();
            if (code)
                return failure("SEND", code);
            av_packet_unref(filtered);
            held_packet = false;
        }
        const int code = collect();
        if (code == AVERROR_EOF)
            return code;
        if (code < 0 && code != AVERROR(EAGAIN))
            return code;
        if (drain_sent || have_frame)
            return 0;
    }
    return failure("FILTER LIMIT", AVERROR_INVALIDDATA);
}
} // namespace
namespace tube5
{
void native_codec_allow_open(bool allow) noexcept
{
    allow_open = allow;
}
bool native_codec_owns(const AVCodecContext *context) noexcept
{
    return owner.load() == context && context;
}
bool native_codec_active() noexcept
{
    return owner.load() != nullptr;
}
unsigned native_codec_reset_generation() noexcept
{
    return reset_generation.load();
}
unsigned native_codec_audio_reset_generation() noexcept
{
    return audio_reset_generation.load();
}
int native_codec_error() noexcept
{
    return owner.load() ? fatal.load() : 0;
}
void native_codec_adopt(AVCodecContext *context) noexcept
{
    if (!allow_open || owner.load() || !context || context->codec_type != AVMEDIA_TYPE_VIDEO ||
        context->codec_id != AV_CODEC_ID_H264 || context->width != 1920 ||
        context->height != 1080 ||
        (context->pix_fmt != AV_PIX_FMT_NONE && context->pix_fmt != AV_PIX_FMT_YUV420P) ||
        context->profile > 100 || context->level > 51)
        return;
    if (!mutex)
        mutex = SDL_CreateMutex();
    if (!mutex)
        return;
    Lock lock;
    fatal = 0;
    delivered = eof_events = skipped = 0;
    // Native fixtures include AUDs; MP4 streams commonly omit them. Preserve
    // compressed pictures while giving the native parser explicit AU boundaries.
    // Extradata may itself contain AUDs, duplicated when parameter sets are
    // prepended to an IDR. Remove existing delimiters before inserting one.
    int code = av_bsf_list_parse_str(
        "h264_mp4toannexb,filter_units=remove_types=9,h264_metadata=aud=insert", &bsf);
    if (!code)
        code = avcodec_parameters_from_context(bsf->par_in, context);
    if (!code)
    {
        bsf->time_base_in = context->pkt_timebase;
        code = av_bsf_init(bsf);
    }
    input = av_packet_alloc();
    filtered = av_packet_alloc();
    pending = av_frame_alloc();
    if (!input || !filtered || !pending)
        code = AVERROR(ENOMEM);
    if (!code)
        code = hardware_stream_begin();
    if (code)
    {
        fatal = code;
        snapshot("SOFTWARE FALLBACK");
        free_filter();
        return;
    }
    owner = context;
    clear_filter_state();
    snapshot("OPEN");
}
int native_codec_send(AVCodecContext *context, const AVPacket *packet) noexcept
{
    Lock lock;
    if (!native_codec_owns(context))
        return AVERROR(EINVAL);
    if (fatal)
        return AVERROR(EIO);
    if (eof)
        return AVERROR_EOF;
    if (have_frame)
        return AVERROR(EAGAIN);
    int code = pump();
    if (code < 0)
        return code;
    if (have_frame || held_packet)
        return AVERROR(EAGAIN);
    if (packet && need_key && !(packet->flags & AV_PKT_FLAG_KEY))
    {
        ++skipped;
        return 0;
    }
    if (packet)
    {
        code = av_packet_ref(input, packet);
        if (code < 0)
            return failure("PACKET REF", code);
    }
    code = av_bsf_send_packet(bsf, packet ? input : nullptr);
    av_packet_unref(input);
    if (code == AVERROR(EAGAIN))
        return code;
    if (code < 0)
        return failure("FILTER SEND", code);
    if (packet)
        need_key = false;
    else
        draining = true;
    code = pump();
    return code == AVERROR_EOF ? 0 : code;
}
int native_codec_receive(AVCodecContext *context, AVFrame *frame) noexcept
{
    Lock lock;
    av_frame_unref(frame);
    if (!native_codec_owns(context))
        return AVERROR(EINVAL);
    if (fatal)
        return AVERROR(EIO);
    if (!have_frame)
    {
        int code = collect();
        if (code < 0 && code != AVERROR(EAGAIN))
            return code;
        if (!have_frame)
        {
            code = pump();
            if (code < 0 && code != AVERROR(EAGAIN))
                return code;
        }
    }
    if (!have_frame)
        return eof ? AVERROR_EOF : AVERROR(EAGAIN);
    av_frame_move_ref(frame, pending);
    have_frame = false;
    if (++delivered % 300 == 0)
        snapshot("PLAYING");
    return 0;
}
} // namespace tube5
extern "C" void __wrap_avcodec_flush_buffers(AVCodecContext *context)
{
    if (tube5::native_codec_owns(context))
    {
        Lock lock;
        if (!fatal)
        {
            const int code = tube5::hardware_stream_reset();
            if (code)
                failure("RESET", code);
            av_bsf_flush(bsf);
            clear_filter_state();
            if (!code)
                ++reset_generation;
            snapshot("RESET");
        }
    }
    __real_avcodec_flush_buffers(context);
    if (tube5::native_codec_active() && context && context->codec_type == AVMEDIA_TYPE_AUDIO)
        ++audio_reset_generation;
}
extern "C" void __wrap_avcodec_free_context(AVCodecContext **context)
{
    if (context && tube5::native_codec_owns(*context))
    {
        Lock lock;
        tube5::hardware_stream_close();
        snapshot("CLOSED");
        free_filter();
        owner = nullptr;
    }
    __real_avcodec_free_context(context);
}
#endif
