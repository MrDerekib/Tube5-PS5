// SPDX-License-Identifier: GPL-3.0-or-later
#include "playback_cadence.hpp"
#include <cassert>
int main()
{
    assert(tube5::cadence_clock_us(UINT64_MAX, 1000000000) == UINT64_MAX / 1000);
    assert(tube5::cadence_clock_us(1234567, 1000000) == 1234567);
    assert(tube5::cadence_clock_us(1, 0) == 0);
    tube5::PlaybackCadence c;
    c.record(100000, 0);
    c.record(116667, 17);
    c.record(133334, 33);
    assert(c.intervals == 2 && c.total_us == 33334 && c.maximum_us == 16667 && c.media_ms == 33);
    c.interrupt();
    c.record(4000000, 100);
    c.record(4016667, 117);
    assert(c.intervals == 3 && c.total_us == 50001 && c.discontinuities == 1);
    c.record(4033334, 0);
    assert(c.invalid_pts == 1 && c.media_ms == 50);
}
