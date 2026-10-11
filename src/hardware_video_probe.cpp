// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware_video_probe.hpp"
#include <cstdio>
#ifdef TUBE5_HW_PROBE
extern "C" int sceSysmoduleLoadModule(std::uint32_t);
extern "C" int sceVideodec2QueryComputeMemoryInfo(tube5::VideoComputeMemory *);
extern "C" int sceVideodec2QueryDecoderMemoryInfo(const tube5::VideoDecoderConfig *,
                                                  tube5::VideoDecoderMemory *);
#endif
namespace tube5
{
void query_hardware_video(HardwareVideoReport &report) noexcept
{
    report = {};
#ifdef TUBE5_HW_PROBE
    report.attempted = true;
    report.module = sceSysmoduleLoadModule(207); // Decimal; process-owned reference.
    if (report.module != 0)
        return;
    report.compute_memory.size = sizeof(VideoComputeMemory);
    report.compute = sceVideodec2QueryComputeMemoryInfo(&report.compute_memory);
    if (report.compute != 0)
        return;
    constexpr int widths[] = {1920, 2560, 3840}, heights[] = {1088, 1440, 2176};
    for (unsigned i = 0; i < report.query.size(); ++i)
    {
        VideoDecoderConfig config{};
        config.size = sizeof(config);
        config.resource = 1;  // Game-process resource; no sandbox escape.
        config.codec = 1;     // AVC
        config.profile = 100; // High
        config.level = i == 2 ? 52 : 51;
        config.width = widths[i];
        config.height = heights[i];
        config.dpb = 4;
        config.depth = 1;
        config.affinity = 0x3f;
        config.priority = 700;
        config.progressive = 1;
        report.memory[i].size = sizeof(VideoDecoderMemory);
        report.query[i] = sceVideodec2QueryDecoderMemoryInfo(&config, &report.memory[i]);
    }
#endif
}
void hardware_video_status(const HardwareVideoReport &report, unsigned row, char *text,
                           std::size_t capacity) noexcept
{
    if (!report.attempted)
        std::snprintf(text, capacity, "HW PROBE NO ACTIVO - PLAYER SOFTWARE");
    else if (row == 0)
        std::snprintf(text, capacity, "HW MODULE %08X COMPUTE %08X BYTES %llu - SOLO CONSULTA",
                      static_cast<unsigned>(report.module), static_cast<unsigned>(report.compute),
                      static_cast<unsigned long long>(report.compute_memory.bytes));
    else if (row <= 3)
    {
        constexpr unsigned height[] = {1080, 1440, 2160};
        const auto &memory = report.memory[row - 1];
        std::snprintf(text, capacity, "HW AVC %uP QUERY %08X FRAME %llu ALIGN %u", height[row - 1],
                      static_cast<unsigned>(report.query[row - 1]),
                      static_cast<unsigned long long>(memory.frame_bytes), memory.alignment);
    }
    else if (capacity)
        text[0] = 0;
}
} // namespace tube5
