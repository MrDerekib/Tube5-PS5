// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace tube5
{
bool memory_out_of_memory() noexcept;
void reset_memory_diagnostic() noexcept;
void memory_set_stage(const char *stage) noexcept;
void memory_mark_playback_output(bool video) noexcept;
void memory_diagnostic_status(char *text, std::size_t capacity) noexcept;
void memory_failure_stage_status(char *text, std::size_t capacity) noexcept;
void memory_allocation_origin_status(unsigned row, char *text, std::size_t capacity) noexcept;
void flexible_memory_status(char *text, std::size_t capacity) noexcept;
} // namespace tube5
