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
enum class HardwareFrameMode
{
    avc1080,
    vp9_1440
};
struct VideoPictureInfo
{
    std::uint64_t size;
    std::uint8_t valid, padding[7];
    std::uint64_t pts, dts, attached;
    std::uint8_t codec_fields[80];
};
static_assert(sizeof(VideoPictureInfo) == 120 && offsetof(VideoPictureInfo, pts) == 16 &&
              offsetof(VideoPictureInfo, attached) == 32);
// One deterministic AU. Optional consumer runs before decoder resources are released.
// Never retry quarantined resources.
int hardware_frame_probe(const char *asset, const char *log,
                         HardwareFrameConsumer consumer = nullptr, void *consumer_context = nullptr,
                         HardwareFrameMode mode = HardwareFrameMode::avc1080) noexcept;
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
    unsigned no_output = 0, drain_calls = 0, metadata = 0, crc_matches = 0;
    unsigned input_reorders = 0, order_errors = 0;
    unsigned coded_frames = 0, hidden_frames = 0, superframes = 0, existing_frames = 0;
    unsigned hidden_no_output = 0, last_valid = 0, last_accepted = 0, last_output_pictures = 0;
    std::uint64_t required_cpu = 0, required_gpu = 0, required_shared = 0, required_frame = 0;
    std::uint64_t last_pts = 0, last_dts = 0, last_attached = 0;
    bool bframes = false;
};
int hardware_clip_begin(const char *asset, const char *log) noexcept;
// Returns zero for a copied frame, one at EOF, two for priming, or a native error.
int hardware_clip_step(HardwareFrameConsumer consumer, void *context) noexcept;
void hardware_clip_presented(std::uint64_t copy_us) noexcept;
int hardware_clip_close(bool cancelled = false, int display_error = 0) noexcept;
const HardwareClipStatus &hardware_clip_status() noexcept;
struct HardwareStreamStatus
{
    bool active = false;
    unsigned submitted = 0, received = 0, resets = 0;
    unsigned accepted = 0, no_output = 0, last_nal_mask = 0;
    unsigned hidden = 0;
    VideoOutput last_output{};
    unsigned last_offered = 0, last_returned = 0, last_frame_accepted = 0;
    std::size_t frame_capacity = 0;
    std::size_t last_au_bytes = 0;
    std::uint64_t last_input_pts = 0, last_input_dts = 0;
    int error = 0, cleanup_error = 0;
    const char *stage = "IDLE";
};
int hardware_stream_begin(HardwareFrameMode mode = HardwareFrameMode::avc1080) noexcept;
int hardware_stream_send(const std::uint8_t *data, std::size_t bytes, std::uint64_t pts,
                         std::uint64_t dts, bool displayable = true) noexcept;
int hardware_stream_receive(VideoOutput &output, VideoPictureInfo &picture) noexcept;
void hardware_stream_release() noexcept;
int hardware_stream_reset() noexcept;
int hardware_stream_close() noexcept;
const HardwareStreamStatus &hardware_stream_status() noexcept;
} // namespace tube5
