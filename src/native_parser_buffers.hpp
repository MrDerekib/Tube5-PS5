// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace tube5
{
bool parser_aligned_caller(void *caller) noexcept;
bool parser_zero_caller(void *caller) noexcept;
void *allocate_parser_buffer(std::size_t bytes, bool zero) noexcept;
bool reallocate_owned_parser_buffer(void *address, std::size_t bytes, void *&result) noexcept;
void native_parser_buffer_status(char *text, std::size_t capacity) noexcept;
} // namespace tube5
