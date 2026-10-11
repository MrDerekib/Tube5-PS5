// SPDX-License-Identifier: GPL-3.0-or-later
#include "playback_progress.hpp"
#include <cassert>
#include <limits>

int main()
{
    using tube5::playback_progress_width;
    constexpr unsigned width = 1220;
    assert(playback_progress_width(0, 600, width) == 0);
    assert(playback_progress_width(300, 600, width) == 610);
    assert(playback_progress_width(150, 600, width) == 305);
    assert(playback_progress_width(600, 600, width) == width);
    assert(playback_progress_width(605, 600, width) == width);
    assert(playback_progress_width(-5, 600, width) == 0);
    assert(playback_progress_width(5, 0, width) == 0);
    assert(playback_progress_width(5, -1, width) == 0);
    assert(playback_progress_width(300, 600, 0) == 0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    assert(playback_progress_width(nan, 600, width) == 0);
    assert(playback_progress_width(5, nan, width) == 0);
    assert(playback_progress_width(inf, 600, width) == 0);
    assert(playback_progress_width(5, inf, width) == 0);
    assert(playback_progress_width(std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::min(), width) == width);
    // Same paused clock yields unchanged geometry; seeks may move either way.
    const unsigned paused = playback_progress_width(60, 600, width);
    assert(playback_progress_width(60, 600, width) == paused);
    assert(playback_progress_width(65, 600, width) > paused);
    assert(playback_progress_width(55, 600, width) < paused);
    for (unsigned second = 0; second <= 600; ++second)
        assert(playback_progress_width(second, 600, width) <= width);
}
