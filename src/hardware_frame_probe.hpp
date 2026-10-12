// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hardware_video_probe.hpp"
#include <cstdint>
namespace tube5
{
struct VideoComputeConfig
{
    std::uint64_t size;
    std::uint16_t pipe, queue;
    std::uint8_t check_memory, reserved0;
    std::uint16_t reserved1;
};
struct VideoInput
{
    std::uint64_t size;
    void *au;
    std::uint64_t bytes, pts, dts, attached;
};
struct VideoFrame
{
    std::uint64_t size;
    void *buffer;
    std::uint64_t bytes;
    std::uint32_t accepted, reserved;
};
struct VideoOutput
{
    std::uint64_t size;
    std::uint8_t valid, error, pictures, padding;
    std::uint32_t codec, width, pitch, height, reserved;
    void *buffer;
    std::uint64_t bytes;
    std::uint32_t format, pitch_bytes;
};
static_assert(sizeof(VideoComputeConfig) == 16 && sizeof(VideoInput) == 48);
static_assert(sizeof(VideoFrame) == 32 && sizeof(VideoOutput) == 56);
static_assert(offsetof(VideoOutput, buffer) == 32);
using HardwareFrameConsumer = int (*)(const VideoOutput &, void *) noexcept;
// One deterministic AU. Optional consumer runs before decoder resources are released.
// Never retry quarantined resources.
int hardware_frame_probe(const char *asset, const char *log,
                         HardwareFrameConsumer consumer = nullptr,
                         void *consumer_context = nullptr) noexcept;
// Eight bounded cycles, stop on the first error, with separate per-cycle reports.
int hardware_frame_cycles(const char *asset, const char *directory) noexcept;
struct HardwareClipStatus
{
    bool active = false, cancelled = false;
    unsigned frames = 0, fps = 0, submitted = 0, decoded = 0, presented = 0;
    unsigned changed_frames = 0, slow_frames = 0;
    int error = 0, cleanup_error = 0;
    std::uint64_t decode_max_us = 0, convert_max_us = 0, copy_max_us = 0, elapsed_us = 0;
    std::uint64_t work_max_us = 0;
};
int hardware_clip_begin(const char *asset, const char *log) noexcept;
// Returns zero for one copied frame, one at EOF, or a negative/native error.
int hardware_clip_step(HardwareFrameConsumer consumer, void *context) noexcept;
void hardware_clip_presented(std::uint64_t copy_us) noexcept;
int hardware_clip_close(bool cancelled = false, int display_error = 0) noexcept;
const HardwareClipStatus &hardware_clip_status() noexcept;
} // namespace tube5
