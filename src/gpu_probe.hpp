// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace tube5
{
struct GpuProbeReport
{
    bool agc_loaded = false, driver_loaded = false, initialized = false, defaults = false;
    std::uint32_t agc_exports = 0, driver_exports = 0;
    int init_result = -1;
    std::uint32_t wait_packet_words = 0;
};
// No queues, shaders, draws, register dereferences or VideoOut modifications.
// Successful system-module references stay owned by this process.
GpuProbeReport probe_gpu(const char *log_path) noexcept;
} // namespace tube5
