// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace tube5
{
// One bounded, atomic latest-failure snapshot, not an unbounded stream log.
// Returns 0 on success or an errno value; never throws or allocates.
int write_failure_log(const char *path, const char *report, std::size_t size) noexcept;
void failure_log_build_label(const char *label) noexcept;
const char *failure_log_build_label() noexcept;
} // namespace tube5
