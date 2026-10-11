// SPDX-License-Identifier: GPL-3.0-or-later
#include "shared_playback_clock.hpp"
extern "C"
{
    struct Kit_Timer;
    Kit_Timer *__real_Kit_CreateTimer();
    Kit_Timer *__real_Kit_CreateSecondaryTimer(const Kit_Timer *, bool);
}
namespace
{
enum class Phase
{
    idle,
    video,
    audio
};
Phase phase = Phase::idle;
Kit_Timer *clock_source = nullptr;
} // namespace
extern "C" Kit_Timer *__wrap_Kit_CreateTimer()
{
    if (phase == Phase::audio)
        return __real_Kit_CreateSecondaryTimer(clock_source, false);
    auto *timer = __real_Kit_CreateTimer();
    if (phase == Phase::video)
        clock_source = timer;
    return timer;
}
extern "C" Kit_Timer *__wrap_Kit_CreateSecondaryTimer(const Kit_Timer *timer, bool writable)
{
    return __real_Kit_CreateSecondaryTimer(timer, phase == Phase::audio ? false : writable);
}
namespace tube5
{
bool create_separate_players(Kit_Source *video, int video_index, Kit_Source *audio, int audio_index,
                             const Kit_VideoFormatRequest *video_request,
                             const Kit_AudioFormatRequest *audio_request, Kit_Player *&video_player,
                             Kit_Player *&audio_player) noexcept
{
    if (phase != Phase::idle || video_player || audio_player)
        return false;
    phase = Phase::video;
    clock_source = nullptr;
    video_player =
        Kit_CreatePlayer(video, video_index, -1, -1, video_request, audio_request, 1920, 1080);
    if (video_player && clock_source)
    {
        phase = Phase::audio;
        audio_player =
            Kit_CreatePlayer(audio, -1, audio_index, -1, video_request, audio_request, 1920, 1080);
    }
    phase = Phase::idle;
    clock_source = nullptr;
    return video_player && audio_player;
}
} // namespace tube5
