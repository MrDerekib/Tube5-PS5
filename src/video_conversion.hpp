// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace tube5
{
void reset_video_conversion() noexcept;
void video_conversion_status(char *text, std::size_t capacity) noexcept;
} // namespace tube5
