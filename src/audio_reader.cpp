// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_reader.hpp"
#ifdef TUBE5_AUDIO_WRAP
#include "audio_reader_abi.hpp"
#include <SDL.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <kitchensink2/kitlib.h>
extern "C"
{
    struct Kit_Decoder;
    struct Kit_Timer;
    struct Kit_PacketBuffer;
    bool Kit_ReadPacketBuffer(Kit_PacketBuffer *, void *, int);
    void Kit_InitTimerBase(Kit_Timer *);
    bool Kit_IsTimerInitialized(const Kit_Timer *);
    bool Kit_IsTimerPrimary(const Kit_Timer *);
    double Kit_GetTimerElapsed(const Kit_Timer *);
    void Kit_AddTimerBase(Kit_Timer *, double);
}
namespace
{
// This reference belongs exclusively to the audio callback. Upstream close
// frees current after SDL_CloseAudioDevice has joined that callback.
AVFrame *retained = nullptr;
} // namespace
namespace tube5
{
void reset_audio_reader() noexcept
{
    if (retained)
        av_frame_unref(retained);
    retained = nullptr;
}
} // namespace tube5
extern "C" int __wrap_Kit_GetAudioDecoderData(Kit_Decoder *decoder, std::size_t queued_bytes,
                                              unsigned char *destination, std::size_t length)
{
    if (!decoder || !destination || !length)
        return 0;
    tube5::audio_detail::DecoderPrefix dec{};
    std::memcpy(&dec, decoder, sizeof(dec));
    tube5::audio_detail::AudioPrefix audio{};
    std::memcpy(&audio, dec.userdata, sizeof(audio));
    if (audio.output.bytes != 2 || audio.output.channels != 2 ||
        audio.output.sample_rate != 48000 || audio.output.format != AUDIO_S16SYS)
        return -1; // Tube5's deliberately fixed packed PCM contract.
    constexpr std::size_t sample_bytes = 4;
    length = std::min(length, static_cast<std::size_t>(INT_MAX));
    length -= length % sample_bytes;
    if (!length)
        return 0;
    auto *timer = static_cast<Kit_Timer *>(dec.timer);
    auto *queue = static_cast<Kit_PacketBuffer *>(audio.buffer);
    AVFrame *frame = audio.current;
    retained = frame;
    Kit_InitTimerBase(timer);
    if (!Kit_IsTimerInitialized(timer))
        return 0;
    const double early = Kit_GetHint(KIT_HINT_AUDIO_EARLY_THRESHOLD) / 1000.0;
    const double late = Kit_GetHint(KIT_HINT_AUDIO_LATE_THRESHOLD) / 1000.0;
    // Bound work in the real-time callback even after a large seek.
    for (unsigned attempts = 0; attempts != 128; ++attempts)
    {
        if (!frame->crop_bottom)
        {
            av_frame_unref(frame);
            // Move ownership and release the queue lock immediately. A partial
            // read remains in current, rather than re-reading or discarding it.
            if (!Kit_ReadPacketBuffer(queue, frame, 0))
                return 0;
        }
        const std::size_t size = frame->crop_top, left = frame->crop_bottom;
        if (!size && !left)
        {
            // Upstream may flush an empty FIFO at EOF.
            av_frame_unref(frame);
            continue;
        }
        if (!frame->data[0] || frame->nb_samples <= 0 || left > size ||
            size != static_cast<std::size_t>(frame->nb_samples) * sample_bytes ||
            left % sample_bytes || frame->linesize[0] < 0 ||
            size > static_cast<std::size_t>(frame->linesize[0]) ||
            frame->best_effort_timestamp == AV_NOPTS_VALUE)
        {
            av_frame_unref(frame);
            return -1;
        }
        const double pts = frame->best_effort_timestamp * av_q2d(dec.stream->time_base) +
                           (size - left) / (48000.0 * sample_bytes);
        double clock = Kit_GetTimerElapsed(timer) + queued_bytes / (48000.0 * sample_bytes);
        if (pts > clock + 5.0)
        {
            if (!Kit_IsTimerPrimary(timer))
            {
                av_frame_unref(frame);
                continue;
            }
            Kit_AddTimerBase(timer, -(pts - clock));
            clock = Kit_GetTimerElapsed(timer) + queued_bytes / (48000.0 * sample_bytes);
        }
        if (pts > clock + early)
            return 0; // Preserve early PCM; caller already zeroes its output.
        if (pts < clock - late)
        {
            av_frame_unref(frame);
            continue;
        }
        const std::size_t copied = std::min(length, left);
        std::memcpy(destination, frame->data[0] + size - left, copied);
        frame->crop_bottom -= copied;
        if (!frame->crop_bottom)
            av_frame_unref(frame);
        return static_cast<int>(copied);
    }
    return 0;
}
#else
namespace tube5
{
void reset_audio_reader() noexcept
{
}
} // namespace tube5
#endif
