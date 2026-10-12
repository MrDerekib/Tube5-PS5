// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_reader.hpp"
#include "audio_reader_abi.hpp"
#include <SDL.h>
#include <cassert>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>
extern "C"
{
#include <kitchensink2/internal/kitdecoder.h>
#include <kitchensink2/internal/audio/kitaudio.h>
#include <kitchensink2/kitlib.h>
    int __wrap_Kit_GetAudioDecoderData(Kit_Decoder *, std::size_t, unsigned char *, std::size_t);
}
using tube5::audio_detail::AudioPrefix;
using tube5::audio_detail::DecoderPrefix;
static_assert(offsetof(DecoderPrefix, timer) == offsetof(Kit_Decoder, sync_timer));
static_assert(offsetof(DecoderPrefix, stream) == offsetof(Kit_Decoder, stream));
static_assert(offsetof(DecoderPrefix, userdata) == offsetof(Kit_Decoder, userdata));
static_assert(sizeof(DecoderPrefix) == offsetof(Kit_Decoder, dec_input));
static double clock_seconds = 0;
#ifdef TUBE5_HW_PLAYER
static unsigned audio_reset = 0;
namespace tube5
{
unsigned native_codec_audio_reset_generation() noexcept
{
    return audio_reset;
}
} // namespace tube5
#endif
extern "C" double __wrap_Kit_GetTimerElapsed(const Kit_Timer *)
{
    return clock_seconds;
}
static void *allocate()
{
    return av_frame_alloc();
}
static void unref(void *frame)
{
    av_frame_unref(static_cast<AVFrame *>(frame));
}
static void release(void **frame)
{
    av_frame_free(reinterpret_cast<AVFrame **>(frame));
}
static void move(void *dst, void *src)
{
    av_frame_move_ref(static_cast<AVFrame *>(dst), static_cast<AVFrame *>(src));
}
static void ref(void *dst, void *src)
{
    assert(av_frame_ref(static_cast<AVFrame *>(dst), static_cast<AVFrame *>(src)) == 0);
}
int main()
{
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    assert(Kit_Init(KIT_INIT_NETWORK) == 0);
    auto *queue = Kit_CreatePacketBuffer(4, allocate, unref, release, move, ref);
    assert(queue);
    AudioPrefix audio{};
    audio.current = av_frame_alloc();
    audio.buffer = queue;
    audio.output = {AUDIO_S16SYS, 1, 2, 48000, 2};
    AVStream stream{};
    stream.time_base = {1, 48000};
    Kit_Decoder decoder{};
    decoder.sync_timer = Kit_CreateTimer();
    decoder.stream = &stream;
    decoder.userdata = &audio;
    auto enqueue = [&](int samples, int pts, unsigned char seed)
    {
        AVFrame *frame = av_frame_alloc();
        frame->format = AV_SAMPLE_FMT_S16;
        frame->sample_rate = 48000;
        frame->nb_samples = samples;
        av_channel_layout_default(&frame->ch_layout, 2);
        assert(av_frame_get_buffer(frame, 0) == 0);
        for (int i = 0; i < samples * 4; ++i)
            frame->data[0][i] = static_cast<unsigned char>(seed + i);
        frame->best_effort_timestamp = pts;
        frame->crop_top = frame->crop_bottom = samples * 4;
        assert(Kit_WritePacketBuffer(queue, frame));
        av_frame_free(&frame);
    };
    std::vector<unsigned char> pcm(2138 * 4);
    enqueue(1114, 0, 7);
    // Reproduce the pinned upstream defect: reading 1024 samples removes an
    // entire 1114-sample packet, throwing away its last 90 stereo samples.
    assert(Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), 4096) == 4096);
    assert(Kit_GetPacketBufferLength(queue) == 0);
    assert(audio.current->crop_bottom == 0);
    enqueue(1114, 0, 7); // Typical 44.1 -> 48 kHz AAC output, not callback-sized.
    enqueue(1024, 1114, 61);
    std::size_t filled = 0;
    while (filled != pcm.size())
    {
        const int count =
            __wrap_Kit_GetAudioDecoderData(&decoder, filled, pcm.data() + filled,
                                           std::min<std::size_t>(1024 * 4, pcm.size() - filled));
        assert(count > 0);
        filled += count;
    }
    for (std::size_t i = 0; i != pcm.size(); ++i)
    {
        const unsigned char expected = i < 1114 * 4 ? 7 + i : 61 + i - 1114 * 4;
        assert(pcm[i] == expected); // No dropped, duplicated, or zero-filled tails.
    }
    assert(!audio.current->crop_bottom);
    AVFrame *empty = av_frame_alloc();
    assert(Kit_WritePacketBuffer(queue, empty));
    av_frame_free(&empty);
    assert(__wrap_Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), pcm.size()) == 0);
    enqueue(1114, 48000, 20);
    assert(__wrap_Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), 4096) == 0); // early retained
    clock_seconds = 1;
    assert(__wrap_Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), 4096) == 4096);
    assert(audio.current->crop_bottom == 360);
    tube5::reset_audio_reader(); // paused seek/close discards only the old tail
    assert(!audio.current->crop_bottom);
    enqueue(1024, 0, 30); // late packet is discarded without retaining a lock
    assert(__wrap_Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), 4096) == 0);
    assert(Kit_GetPacketBufferLength(queue) == 0);
    enqueue(1024, 48000, 40);
    assert(__wrap_Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), 3) == 0); // frame alignment
    assert(__wrap_Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), 4096) == 4096);
    tube5::reset_audio_reader();
#ifdef TUBE5_HW_PLAYER
    // A blocked producer must be able to drain old PCM and reach its reset.
    clock_seconds = 100;
    enqueue(1024, 48000 * 100, 51);
    tube5::hold_audio_reader(audio_reset);
    assert(__wrap_Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), 4096) == 0);
    assert(Kit_GetPacketBufferLength(queue) == 0);
    ++audio_reset;
    enqueue(1024, 48000 * 10, 52); // Backwards seek: new PCM vs the old 100s clock.
    assert(__wrap_Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), 4096) == 0);
    assert(Kit_GetPacketBufferLength(queue) == 1); // Must not discard post-reset PCM.
    tube5::resume_audio_reader();
    clock_seconds = 10;
    assert(__wrap_Kit_GetAudioDecoderData(&decoder, 0, pcm.data(), 4096) == 4096);
    assert(pcm[0] == 52);
    tube5::reset_audio_reader();
#endif
    av_frame_free(&audio.current);
    Kit_FreePacketBuffer(&queue);
    Kit_CloseTimer(&decoder.sync_timer);
    Kit_Quit();
    SDL_Quit();
}
