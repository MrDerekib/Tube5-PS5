// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_video_codec.hpp"
#ifdef TUBE5_HW_PLAYER
extern "C"
{
    struct Kit_Timer;
    void __real_Kit_AdjustTimerBase(Kit_Timer *, double);
    void __wrap_Kit_AdjustTimerBase(Kit_Timer *timer, double value)
    {
        __real_Kit_AdjustTimerBase(timer, tube5::native_codec_seek_clock_adjust(value));
    }
}
#endif
