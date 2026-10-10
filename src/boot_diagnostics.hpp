// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// Optional checkpoints must work before runtime/heap initialization.
extern "C" void tube5_boot_checkpoint(const char *message) noexcept;
void tube5_boot_result(const char *stage, std::uint32_t result) noexcept;
