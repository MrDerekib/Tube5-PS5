// SPDX-License-Identifier: GPL-3.0-or-later
#include "seek_clock_rebase.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
struct Kit_Timer { double *clock; bool writable; };
static tube5::SeekClockRebase rebase;
static std::uint64_t worker = 10;
namespace tube5
{
double native_codec_seek_clock_adjust(double value) noexcept { return rebase.apply(worker, value); }
}
extern "C" void __wrap_Kit_AdjustTimerBase(Kit_Timer *, double);
extern "C" void __real_Kit_AdjustTimerBase(Kit_Timer *timer, double value)
{
    if (timer->writable) *timer->clock = value;
}
static bool near(double a, double b) { return std::abs(a - b) < 0.000001; }
int main()
{
    double clock = 0;
    Kit_Timer video{&clock, true}, audio{&clock, false};
    __wrap_Kit_AdjustTimerBase(&video, 12.0);
    assert(near(clock, 12.0) && !rebase.applied());
    rebase.arm(10, 20.0);
    worker = 11; // Audio cannot consume the video worker's one-shot adjustment.
    __wrap_Kit_AdjustTimerBase(&audio, 19.9);
    assert(near(clock, 12.0) && rebase.eligible(10));
    worker = 10;
    __wrap_Kit_AdjustTimerBase(&video, 7.0); // Unrelated UI/timer rebase untouched.
    assert(near(clock, 7.0) && rebase.eligible(10));
    __wrap_Kit_AdjustTimerBase(&video, 19.9);
    assert(near(clock, 20.1) && rebase.applied() == 1);
    // Both readers follow the same shared clock. It now matches the baseline
    // clock after its 200ms wait; do not invent a +0.1 offset in the audio reader.
    assert(near(20.0 + 0.1 - *video.clock, 0));
    assert(near(*audio.clock, 19.9 + 0.2));
    __wrap_Kit_AdjustTimerBase(&video, 19.9);
    assert(near(clock, 19.9) && rebase.applied() == 1); // One shot.
    rebase.arm(10, 30.0);
    rebase.cancel();
    assert(near(rebase.apply(10, 29.9), 29.9));
    rebase.arm(10, 40.0);
    rebase.arm(10, 50.0); // New seek cannot consume the old target's rebase.
    assert(near(rebase.apply(10, 39.9), 39.9) && rebase.eligible(10));
    assert(near(rebase.apply(10, 49.9), 50.1));
    rebase.arm(10, 0.0);
    assert(near(rebase.apply(10, -0.1), 0.1));
    rebase.arm(0, 10.0);
    assert(!rebase.eligible(0));
    rebase.arm(10, std::numeric_limits<double>::quiet_NaN());
    assert(!rebase.eligible(10));
    rebase.arm(10, 20.0);
    assert(std::isnan(rebase.apply(10, std::numeric_limits<double>::quiet_NaN())));
    assert(near(rebase.apply(10, 20.0), 20.0)); // Other dependency offset: untouched.
    rebase.reset();
    assert(!rebase.applied() && !rebase.eligible(10));
    std::puts("Seek clock wrapper: one shot, shared A/V, thread/PTS guards, supersede, cancel and reset passed");
}
