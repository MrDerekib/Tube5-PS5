// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Optional checkpoints must work before runtime/heap initialization.
extern "C" void tube5_boot_checkpoint(const char *message) noexcept;
