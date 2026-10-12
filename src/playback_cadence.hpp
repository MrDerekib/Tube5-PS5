// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace tube5
{
inline std::uint64_t cadence_clock_us(std::uint64_t counter, std::uint64_t frequency) noexcept
{
    return frequency ? (counter / frequency) * 1000000 + (counter % frequency) * 1000000 / frequency
                     : 0;
}
struct PlaybackCadence
{
    std::uint64_t previous_us = 0, intervals = 0, total_us = 0, maximum_us = 0;
    std::int64_t previous_pts = -1, media_ms = 0;
    unsigned discontinuities = 0, invalid_pts = 0;
    bool have_previous = false;
    void interrupt() noexcept
    {
        have_previous = false;
        ++discontinuities;
    }
    void record(std::uint64_t now, std::int64_t pts) noexcept
    {
        if (have_previous && now >= previous_us)
        {
            const auto delta = now - previous_us;
            total_us += delta;
            if (delta > maximum_us)
                maximum_us = delta;
            ++intervals;
            if (pts >= 0 && previous_pts >= 0 && pts >= previous_pts)
                media_ms += pts - previous_pts;
            else
                ++invalid_pts;
        }
        previous_us = now;
        previous_pts = pts;
        have_previous = true;
    }
};
} // namespace tube5
