// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace tube5::seek_preroll
{
// One player, two decoder workers. Reset only before start or after joins.
inline std::atomic<std::int64_t> target_us{AV_NOPTS_VALUE};
inline void reset() noexcept
{
    target_us = AV_NOPTS_VALUE;
}
inline void set(const AVFormatContext *format, int stream, std::int64_t target, int flags) noexcept
{
    if (!format || (flags & (AVSEEK_FLAG_BYTE | AVSEEK_FLAG_FRAME)))
        return;
    AVRational time_base = AV_TIME_BASE_Q;
    if (stream >= 0)
    {
        if (static_cast<unsigned>(stream) >= format->nb_streams)
            return;
        time_base = format->streams[stream]->time_base;
    }
    if (time_base.num > 0 && time_base.den > 0)
        target_us = av_rescale_q(target, time_base, AV_TIME_BASE_Q);
}
inline bool discard(const AVCodecContext *codec, const AVFrame *frame) noexcept
{
    const auto target = target_us.load();
    if (target == AV_NOPTS_VALUE || !codec || !frame ||
        (codec->codec_type != AVMEDIA_TYPE_VIDEO && codec->codec_type != AVMEDIA_TYPE_AUDIO) ||
        codec->pkt_timebase.num <= 0 || codec->pkt_timebase.den <= 0)
        return false;
    const auto timestamp =
        frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
    return timestamp != AV_NOPTS_VALUE &&
           av_compare_ts(timestamp, codec->pkt_timebase, target, AV_TIME_BASE_Q) < 0;
}
} // namespace tube5::seek_preroll
