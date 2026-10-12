// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace tube5
{
void reset_video_sync_diagnostic() noexcept;
void video_sync_time_base(int numerator, int denominator) noexcept;
void video_sync_diagnostic_status(char *text, std::size_t capacity) noexcept;
int video_sync_frame_ms() noexcept;
unsigned video_sync_drops() noexcept;
} // namespace tube5
