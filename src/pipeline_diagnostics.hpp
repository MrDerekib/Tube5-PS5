// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace tube5
{
// Sessions begin only after source probing, and end after workers are joined.
void begin_pipeline_diagnostic() noexcept;
void end_pipeline_diagnostic() noexcept;
void pipeline_diagnostic_status(unsigned row, char *text, std::size_t capacity) noexcept;
} // namespace tube5
