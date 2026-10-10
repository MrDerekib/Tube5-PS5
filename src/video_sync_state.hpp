// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
namespace tube5::video_sync_detail
{
extern std::atomic<unsigned> drops, waits, empty_reads;
extern std::atomic<int> time_num, time_den, frame_ms, clock_ms;
} // namespace tube5::video_sync_detail
