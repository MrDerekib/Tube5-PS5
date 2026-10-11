// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace tube5
{
// Interface layout reference: BlackBearReloaded/ProsperoLight (GPL-3.0-or-later).
struct VideoDecoderConfig
{
    std::uint64_t size;
    std::uint32_t resource, codec, profile, level;
    std::int32_t width, height, dpb;
    std::uint32_t depth;
    std::uint64_t queue, affinity;
    std::int32_t priority;
    std::uint32_t progressive, check_memory, reserved;
};
struct VideoDecoderMemory
{
    std::uint64_t size, cpu_bytes;
    void *cpu;
    std::uint64_t gpu_bytes;
    void *gpu;
    std::uint64_t shared_bytes;
    void *shared;
    std::uint64_t frame_bytes;
    std::uint32_t alignment, reserved;
};
struct VideoComputeMemory
{
    std::uint64_t size, bytes;
    void *shared;
};
static_assert(sizeof(VideoDecoderConfig) == 72 && offsetof(VideoDecoderConfig, queue) == 40);
static_assert(sizeof(VideoDecoderMemory) == 72 && offsetof(VideoDecoderMemory, frame_bytes) == 56);
static_assert(sizeof(VideoComputeMemory) == 24);
struct HardwareVideoReport
{
    bool attempted = false;
    int module = -1, compute = -1;
    VideoComputeMemory compute_memory{};
    std::array<int, 3> query{-1, -1, -1};
    std::array<VideoDecoderMemory, 3> memory{};
};
void query_hardware_video(HardwareVideoReport &report) noexcept;
void hardware_video_status(const HardwareVideoReport &report, unsigned row, char *text,
                           std::size_t capacity) noexcept;
} // namespace tube5
