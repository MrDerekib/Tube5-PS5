// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware_frame_probe.hpp"
#include "gpu_presenter.hpp"
#include "failure_log.hpp"
#include <cstdlib>
#include <cstdio>
#include <algorithm>
extern "C" int sceSysmoduleLoadModule(unsigned);
extern "C" int sceKernelUsleep(unsigned);
extern "C" int sceSystemServiceHideSplashScreen();
namespace
{
std::uint32_t *background = nullptr;
std::uint32_t frame_crc = 0, expected_crc = 0;
int show(const tube5::VideoOutput &frame, void *) noexcept
{
    if (!frame.buffer || frame.width != 2560 || frame.height != 1440 || frame.format != 0 ||
        frame.pitch_bytes < 2560 || frame.pitch_bytes > 4096)
        return -22;
    auto *y = static_cast<unsigned char *>(frame.buffer);
    unsigned char *planes[2] = {y, y + static_cast<std::size_t>(frame.pitch_bytes) * frame.height};
    frame_crc = UINT32_MAX;
    for (unsigned row = 0; row < 1440; row += 31)
        for (unsigned col = 0; col < 2560; col += 31)
        {
            const auto chroma = (row / 2) * frame.pitch_bytes + (col / 2) * 2;
            const unsigned char samples[3] = {y[row * frame.pitch_bytes + col], planes[1][chroma],
                                              planes[1][chroma + 1]};
            for (const auto value : samples)
            {
                frame_crc ^= value;
                for (unsigned bit = 0; bit < 8; ++bit)
                    frame_crc = (frame_crc >> 1) ^ ((frame_crc & 1u) ? UINT32_C(0xedb88320) : 0);
            }
        }
    frame_crc ^= UINT32_MAX;
    if (frame_crc != expected_crc)
        return -84;
    const int pitches[2] = {static_cast<int>(frame.pitch_bytes),
                            static_cast<int>(frame.pitch_bytes)};
    tube5::gpu_begin_frame();
    if (!tube5::gpu_upload_nv12(planes, pitches, 2560, 1440, true, false))
        return -5;
    tube5::gpu_draw_video(SDL_Rect{0, 0, 1920, 1080});
    return tube5::gpu_present_ui(background) ? 0 : -5;
}
} // namespace
int main()
{
    tube5::failure_log_build_label("BOOT-DIAG-76-VP9-1440");
    int result = sceSysmoduleLoadModule(207);
    if (!result)
    {
        FILE *reference = std::fopen("/app0/assets/hw-vp9-1440.crc", "r");
        unsigned value = 0;
        if (!reference || std::fscanf(reference, "%8x", &value) != 1)
            result = -22;
        else
            expected_crc = value;
        if (reference)
            std::fclose(reference);
    }
    if (!result)
    {
        background = static_cast<std::uint32_t *>(std::calloc(1920u * 1080u, 4));
        if (!background)
            result = -12;
    }
    if (!result && !tube5::gpu_presenter_init())
        result = -5;
    if (!result)
    {
        (void)sceSystemServiceHideSplashScreen();
        result = tube5::hardware_frame_probe("/app0/assets/hw-vp9-1440.frame",
                                             "/download0/tube5-vp9-1440-decoder.log", show, nullptr,
                                             tube5::HardwareFrameMode::vp9_1440);
    }
    char report[384]{};
    const int n =
        std::snprintf(report, sizeof(report),
                      "TUBE5 VP9 1440 PROBE V1\nBUILD BOOT-DIAG-76-VP9-1440\nRESULT %d\nGPU %s\n"
                      "ONE VP9 PROFILE 0 KEYFRAME 2560x1440 - DISPLAY TARGET 1920x1080\n"
                      "SAMPLED YUV CRC %08X EXPECTED %08X\n"
                      "NOT CONTINUOUS PLAYBACK - NOT YOUTUBE OR AUDIO TEST\n",
                      result, tube5::gpu_presenter_error()[0] ? tube5::gpu_presenter_error() : "OK",
                      frame_crc, expected_crc);
    if (n > 0)
        (void)tube5::write_failure_log("/download0/tube5-vp9-1440.log", report,
                                       std::min<std::size_t>(n, sizeof(report) - 1));
    for (;;)
        sceKernelUsleep(16000);
}
