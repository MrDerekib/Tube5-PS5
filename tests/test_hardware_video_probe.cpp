// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware_video_probe.hpp"
#include <cassert>
#include <cstring>
int module_error = 0, compute_error = 0, query_error = 0;
unsigned module_calls = 0, compute_calls = 0, query_calls = 0;
extern "C" int sceSysmoduleLoadModule(std::uint32_t id)
{
    assert(id == 207);
    ++module_calls;
    return module_error;
}
extern "C" int sceVideodec2QueryComputeMemoryInfo(tube5::VideoComputeMemory *memory)
{
    assert(memory->size == 24 && !memory->bytes && !memory->shared);
    ++compute_calls;
    memory->bytes = 16384;
    return compute_error;
}
extern "C" int sceVideodec2QueryDecoderMemoryInfo(const tube5::VideoDecoderConfig *config,
                                                  tube5::VideoDecoderMemory *memory)
{
    assert(config->size == 72 && config->resource == 1 && config->codec == 1 &&
           config->profile == 100 && config->dpb == 4 && config->depth == 1 && !config->queue &&
           config->affinity == 0x3f && config->priority == 700 && config->progressive == 1 &&
           !config->check_memory && !config->reserved);
    const auto i = query_calls++ % 3;
    constexpr int widths[] = {1920, 2560, 3840}, heights[] = {1088, 1440, 2176};
    assert(config->width == widths[i] && config->height == heights[i]);
    assert(config->level == (i == 2 ? 52u : 51u));
    assert(memory->size == 72 && !memory->cpu && !memory->gpu && !memory->shared);
    memory->frame_bytes = 4096 * (i + 1);
    memory->alignment = 16384;
    return i == 1 ? query_error : 0;
}
int main()
{
    tube5::HardwareVideoReport report;
    tube5::query_hardware_video(report);
#ifndef TUBE5_HW_PROBE
    assert(!report.attempted && !module_calls && !query_calls && !compute_calls);
    char status[128]{};
    tube5::hardware_video_status(report, 0, status, sizeof(status));
    assert(std::strstr(status, "PLAYER SOFTWARE"));
#else
    assert(report.attempted && report.module == 0 && report.compute == 0 && query_calls == 3);
    char status[192]{};
    tube5::hardware_video_status(report, 3, status, sizeof(status));
    assert(std::strstr(status, "2160P QUERY 00000000 FRAME 12288 ALIGN 16384"));
    module_error = -7;
    tube5::query_hardware_video(report);
    assert(report.module == -7 && report.compute == -1 && report.query[0] == -1 &&
           compute_calls == 1 && query_calls == 3 && !report.memory[0].frame_bytes);
    module_error = 0;
    compute_error = -8;
    tube5::query_hardware_video(report);
    assert(report.compute == -8 && query_calls == 3);
    compute_error = 0;
    query_error = -9;
    tube5::query_hardware_video(report);
    assert(report.query[0] == 0 && report.query[1] == -9 && report.query[2] == 0);
    assert(query_calls == 6 && module_calls == 4);
    tube5::hardware_video_status(report, 99, status, sizeof(status));
    assert(!status[0]);
#endif
}
