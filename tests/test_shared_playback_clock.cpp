// SPDX-License-Identifier: GPL-3.0-or-later
#include "shared_playback_clock.hpp"
#include <array>
#include <cassert>
#include <cstdint>
struct Kit_Timer
{
    const Kit_Timer *reference;
    bool writable;
};
static std::array<Kit_Timer, 32> timers{};
static unsigned used = 0, players = 0;
static bool fail_audio = false;
static Kit_Timer *video_clock = nullptr, *audio_clock = nullptr, *audio_decoder_clock = nullptr;
extern "C" Kit_Timer *__wrap_Kit_CreateTimer();
extern "C" Kit_Timer *__wrap_Kit_CreateSecondaryTimer(const Kit_Timer *, bool);
extern "C" Kit_Timer *__real_Kit_CreateTimer()
{
    auto *timer = &timers[used++];
    *timer = {timer, true};
    return timer;
}
extern "C" Kit_Timer *__real_Kit_CreateSecondaryTimer(const Kit_Timer *source, bool writable)
{
    auto *timer = &timers[used++];
    *timer = {source->reference, writable};
    return timer;
}
extern "C" Kit_Player *Kit_CreatePlayer(const Kit_Source *, int video, int audio, int,
                                        const Kit_VideoFormatRequest *,
                                        const Kit_AudioFormatRequest *, int, int)
{
    auto *clock = __wrap_Kit_CreateTimer();
    if (video >= 0)
        video_clock = clock;
    if (audio >= 0)
    {
        audio_clock = clock;
        audio_decoder_clock = __wrap_Kit_CreateSecondaryTimer(clock, true);
        if (fail_audio)
            return nullptr;
    }
    return reinterpret_cast<Kit_Player *>(static_cast<std::uintptr_t>(++players));
}
int main()
{
    Kit_Source source{};
    Kit_Player *video = nullptr, *audio = nullptr;
    assert(tube5::create_separate_players(&source, 0, &source, 0, nullptr, nullptr, video, audio));
    assert(video_clock->writable && !audio_clock->writable && !audio_decoder_clock->writable);
    assert(video_clock->reference == audio_clock->reference &&
           audio_clock->reference == audio_decoder_clock->reference);
    assert(__wrap_Kit_CreateTimer()->writable);
    assert(__wrap_Kit_CreateSecondaryTimer(video_clock, true)->writable);
    assert(!tube5::create_separate_players(&source, 0, &source, 0, nullptr, nullptr, video, audio));
    video = audio = nullptr;
    fail_audio = true;
    assert(!tube5::create_separate_players(&source, 0, &source, 0, nullptr, nullptr, video, audio));
    assert(video && !audio);                    // Caller owns partial construction and closes it.
    assert(__wrap_Kit_CreateTimer()->writable); // Failure cannot leave the hook active.
}
