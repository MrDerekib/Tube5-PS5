// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace tube5
{
void *allocate_refstruct_buffer(std::size_t bytes, void *caller) noexcept;
void native_refstruct_buffer_status(char *text, std::size_t capacity) noexcept;
} // namespace tube5
