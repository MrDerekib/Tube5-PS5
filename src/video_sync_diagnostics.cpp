// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_sync_diagnostics.hpp"
#include "video_sync_state.hpp"
#include <atomic>
#include <cstdio>

namespace tube5::video_sync_detail
{
std::atomic<unsigned> drops{0}, waits{0}, empty_reads{0};
std::atomic<int> time_num{0}, time_den{0}, frame_ms{-1}, clock_ms{-1};
} // namespace tube5::video_sync_detail
namespace tube5
{
using namespace video_sync_detail;
void reset_video_sync_diagnostic() noexcept
{
    drops = waits = empty_reads = 0;
    time_num = time_den = 0;
    frame_ms = clock_ms = -1;
}
void video_sync_time_base(int numerator, int denominator) noexcept
{
    if (numerator > 0 && denominator > 0)
    {
        time_num = numerator;
        time_den = denominator;
    }
}
void video_sync_diagnostic_status(char *text, std::size_t capacity) noexcept
{
    std::snprintf(text, capacity, "SYNC PTS %d CLOCK %d DROP %u WAIT %u EMPTY %u", frame_ms.load(),
                  clock_ms.load(), drops.load(), waits.load(), empty_reads.load());
}
} // namespace tube5
