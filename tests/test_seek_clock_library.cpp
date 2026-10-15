// SPDX-License-Identifier: GPL-3.0-or-later
#include "seek_clock_rebase.hpp"
#include <cassert>
#include <cstdio>
extern "C"
{
    struct Kit_Timer;
    Kit_Timer *Kit_CreateTimer();
    Kit_Timer *Kit_CreateSecondaryTimer(const Kit_Timer *, bool);
    void Kit_AdjustTimerBase(Kit_Timer *, double);
    double Kit_GetTimerElapsed(const Kit_Timer *);
    void Kit_CloseTimer(Kit_Timer **);
}
static double now = 1000;
static tube5::SeekClockRebase rebase;
static std::uint64_t worker = 10;
extern "C" double Kit_GetSystemTime() { return now; }
namespace tube5
{
double native_codec_seek_clock_adjust(double value) noexcept { return rebase.apply(worker, value); }
}
static bool near(double a, double b) { return std::abs(a - b) < 0.000001; }
int main()
{
    auto *video = Kit_CreateTimer();
    assert(video);
    auto *audio = Kit_CreateSecondaryTimer(video, false);
    assert(audio);
    Kit_AdjustTimerBase(video, 19.9);
    assert(near(Kit_GetTimerElapsed(video), 19.9));
    now += 0.2;
    const double baseline = Kit_GetTimerElapsed(video);
    assert(near(baseline, 20.1));
    now = 1001;
    rebase.arm(10, 20.0);
    Kit_AdjustTimerBase(video, 19.9);
    assert(rebase.applied() == 1);
    assert(near(Kit_GetTimerElapsed(video), baseline));
    assert(near(Kit_GetTimerElapsed(audio), baseline));
    worker = 11;
    Kit_AdjustTimerBase(audio, 700); // Real read-only follower still cannot rebase A/V.
    assert(near(Kit_GetTimerElapsed(video), baseline));
    now += 0.016;
    assert(near(Kit_GetTimerElapsed(video), baseline + 0.016));
    assert(near(Kit_GetTimerElapsed(audio), baseline + 0.016));
    Kit_CloseTimer(&audio);
    Kit_CloseTimer(&video);
    assert(!audio && !video);
    std::puts("Pinned Kitchensink timer: 200ms wait removed, shared A/V reference/read-only/lifecycle preserved");
}
