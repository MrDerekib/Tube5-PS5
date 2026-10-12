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
extern "C" unsigned long long sceKernelGetProcessTime();
namespace
{
std::uint32_t *background = nullptr;
std::uint64_t epoch = 0, previous = 0, last_work = 0;
std::uint64_t work[90]{}, intervals[90]{};
unsigned shown = 0, late = 0;
int show(const tube5::VideoOutput &frame, void *) noexcept
{
    if (!frame.buffer || frame.width != 3840 || frame.height != 2160 || frame.format != 0 ||
        frame.pitch_bytes < 3840 || frame.pitch_bytes > 4096 || shown >= 90)
        return -22;
    auto *y = static_cast<unsigned char *>(frame.buffer);
    unsigned char *planes[2] = {y, y + static_cast<std::size_t>(frame.pitch_bytes) * frame.height};
    const int pitches[2] = {static_cast<int>(frame.pitch_bytes),
                            static_cast<int>(frame.pitch_bytes)};
    const auto start = sceKernelGetProcessTime();
    tube5::gpu_begin_frame();
    if (!tube5::gpu_upload_nv12(planes, pitches, 3840, 2160, true, false))
        return -5;
    tube5::gpu_draw_video(SDL_Rect{0, 0, 1920, 1080});
    if (!tube5::gpu_present_ui(background))
        return -5;
    const auto now = sceKernelGetProcessTime();
    last_work = now - start;
    work[shown] = last_work;
    if (shown)
        intervals[shown - 1] = now - previous;
    else
        epoch = now;
    previous = now;
    ++shown;
    return 0;
}
} // namespace
int main()
{
    tube5::failure_log_build_label("BOOT-DIAG-85-VP9-4K");
    int result = sceSysmoduleLoadModule(207);
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
        result = tube5::hardware_clip_begin("/app0/assets/hw-vp9-4k.t5hc",
                                            "/download0/tube5-vp9-4k-decoder.log");
    }
    while (!result)
    {
        if (shown)
        {
            const auto target = epoch + shown * UINT64_C(1000000) / 30;
            const auto now = sceKernelGetProcessTime();
            if (now < target)
                sceKernelUsleep(static_cast<unsigned>(target - now));
            else if (now - target > 2000)
                ++late;
        }
        result = tube5::hardware_clip_step(show, nullptr);
        if (!result)
            tube5::hardware_clip_presented(last_work);
    }
    if (result == 1)
        result = 0;
    const auto elapsed = shown > 1 ? previous - epoch : 0;
    const int cleanup =
        tube5::hardware_clip_close(false, tube5::hardware_clip_status().error ? 0 : result);
    if (shown > 1)
    {
        std::sort(work + 1, work + shown);
        std::sort(intervals, intervals + shown - 1);
    }
    const auto &status = tube5::hardware_clip_status();
    char report[640]{};
    const int n = std::snprintf(
        report, sizeof(report),
        "TUBE5 VP9 CLIP V3\nBUILD BOOT-DIAG-85-VP9-4K\nRESULT %d CLEANUP %d GPU %s\n"
        "SOURCE VP9 PROFILE 0 3840x2160 TARGET 1920x1080 FPS 30\n"
        "SHOWN %u CRC MATCHES %u ELAPSED BETWEEN FLIPS US %llu LATE %u\n"
        "PRESENT WORK P50/P95/MAX US %llu/%llu/%llu\n"
        "FLIP INTERVAL P50/P95/MAX US %llu/%llu/%llu\n"
        "CODED %u HIDDEN %u SUPERFRAMES %u EXISTING %u\nVISIBLE FRAME PROOF - NO "
        "AUDIO/YOUTUBE/HIDDEN TEST\n",
        result, cleanup, tube5::gpu_presenter_error()[0] ? tube5::gpu_presenter_error() : "OK",
        shown, status.crc_matches, static_cast<unsigned long long>(elapsed), late,
        static_cast<unsigned long long>(shown > 1 ? work[1 + (shown - 2) / 2] : 0),
        static_cast<unsigned long long>(shown > 1 ? work[1 + (shown - 2) * 95 / 100] : 0),
        static_cast<unsigned long long>(shown > 1 ? work[shown - 1] : 0),
        static_cast<unsigned long long>(shown > 1 ? intervals[(shown - 2) / 2] : 0),
        static_cast<unsigned long long>(shown > 1 ? intervals[(shown - 2) * 95 / 100] : 0),
        static_cast<unsigned long long>(shown > 1 ? intervals[shown - 2] : 0), status.coded_frames,
        status.hidden_frames, status.superframes, status.existing_frames);
    if (n > 0)
        (void)tube5::write_failure_log("/download0/tube5-vp9-4k.log", report,
                                       std::min<std::size_t>(n, sizeof(report) - 1));
    for (;;)
        sceKernelUsleep(16000);
}
