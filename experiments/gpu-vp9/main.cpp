// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware_frame_probe.hpp"
#include "gpu_presenter.hpp"
#include "failure_log.hpp"
#include "demo_renderer.hpp"
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
std::uint64_t work[180]{}, intervals[180]{};
std::uint64_t phases[7][180]{};
unsigned shown = 0, late = 0;
int show(const tube5::VideoOutput &frame, void *) noexcept
{
    if (!frame.buffer || frame.width != 3840 || frame.height != 2160 || frame.format != 0 ||
        frame.pitch_bytes < 3840 || frame.pitch_bytes > 4096 || shown >= 180)
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
    tube5::failure_log_build_label("BOOT-DIAG-100-VP9-FLAG-POLICY");
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
        result = tube5::hardware_clip_begin("/app0/assets/hw-vp9-4k60-altref-tc2.t5hc",
                                            "/download0/tube5-vp9-depth3-decoder.log");
    }
    while (!result)
    {
        if (shown && shown < 180)
        {
            const auto sleep_start = sceKernelGetProcessTime();
            const auto target = epoch + shown * UINT64_C(1000000) / 60;
            const auto now = sceKernelGetProcessTime();
            if (now < target)
            {
                sceKernelUsleep(static_cast<unsigned>(target - now));
                phases[6][shown] = sceKernelGetProcessTime() - sleep_start;
            }
            else if (now - target > 2000)
                ++late;
        }
        result = tube5::hardware_clip_step(show, nullptr);
        if (result == 2)
        {
            result = 0; // Decoder priming/drain is not EOF; retained buffers stay owned.
            continue;
        }
        if (!result)
        {
            const auto &s = tube5::hardware_clip_status();
            const unsigned i = shown - 1;
            phases[0][i] = s.last_read_us;
            phases[1][i] = s.last_decode_us;
            phases[2][i] = s.last_crc_us;
            phases[3][i] = s.last_consumer_us;
            phases[4][i] = s.last_step_us;
            phases[5][i] = s.last_log_us;
            tube5::hardware_clip_presented(0); // Consumer already includes presentation/copy.
        }
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
    char report[2560]{};
    const int n = std::snprintf(
        report, sizeof(report),
        "TUBE5 VP9 FLAG POLICY - CRC INDEPENDENT VERIFIER\nBUILD BOOT-DIAG-100-VP9-FLAG-POLICY\nRESULT %d CLEANUP %d GPU %s\n"
        "SOURCE VP9 PROFILE 0 3840x2160 TARGET 1920x1080 FPS 60\n"
        "SHOWN %u CRC MATCHES %u ELAPSED BETWEEN FLIPS US %llu LATE %u\n"
        "PRESENT WORK P50/P95/MAX US %llu/%llu/%llu\n"
        "FLIP INTERVAL P50/P95/MAX US %llu/%llu/%llu\n"
        "CODED %u HIDDEN %u SUPERFRAMES %u EXISTING %u\nVISIBLE FRAME PROOF - NO "
        "AUDIO/YOUTUBE TEST - HIDDEN CORRELATION PROBE\n",
        result, cleanup, tube5::gpu_presenter_error()[0] ? tube5::gpu_presenter_error() : "OK",
        shown, status.crc_matches, static_cast<unsigned long long>(elapsed), late,
        static_cast<unsigned long long>(shown > 1 ? work[1 + (shown - 2) / 2] : 0),
        static_cast<unsigned long long>(shown > 1 ? work[1 + (shown - 2) * 95 / 100] : 0),
        static_cast<unsigned long long>(shown > 1 ? work[shown - 1] : 0),
        static_cast<unsigned long long>(shown > 1 ? intervals[(shown - 2) / 2] : 0),
        static_cast<unsigned long long>(shown > 1 ? intervals[(shown - 2) * 95 / 100] : 0),
        static_cast<unsigned long long>(shown > 1 ? intervals[shown - 2] : 0), status.coded_frames,
        status.hidden_frames, status.superframes, status.existing_frames);
    std::size_t used = n > 0 ? std::min<std::size_t>(n, sizeof(report) - 1) : 0;
    const char *names[]{"READ", "DECODE", "CRC", "CONSUMER", "STEP_TOTAL", "LOG", "PACE_SLEEP"};
    if (shown > 1)
        for (unsigned p = 0; p < 7; ++p)
        {
            const unsigned count = shown - 1;
            std::uint64_t sum = 0;
            for (unsigned i = 1; i < shown; ++i)
                sum += phases[p][i];
            std::sort(phases[p] + 1, phases[p] + shown);
            const int added = std::snprintf(
                report + used, sizeof(report) - used,
                "PHASE %s N %u AVG US %llu P50/P95/MAX US %llu/%llu/%llu\n", names[p], count,
                static_cast<unsigned long long>(sum / count),
                static_cast<unsigned long long>(phases[p][1 + (count - 1) / 2]),
                static_cast<unsigned long long>(phases[p][1 + (count - 1) * 95 / 100]),
                static_cast<unsigned long long>(phases[p][shown - 1]));
            if (added > 0)
                used += std::min<std::size_t>(added, sizeof(report) - used - 1);
        }
    if (used)
        (void)tube5::write_failure_log("/download0/tube5-vp9-depth3.log", report, used);
    if (background)
    {
        const bool success = !result && !cleanup && shown == 180 && status.crc_matches == 180;
        ps5::demo::Canvas canvas(background, true);
        canvas.clear(ps5::demo::Color::background);
        canvas.text(100, 160, success ? "FINALIZADA" : "ERROR", 10,
                    success ? ps5::demo::Color::cyan : ps5::demo::Color::yellow);
        char outcome[128]{};
        std::snprintf(outcome, sizeof(outcome), "FRAMES %u/180 - CRC %u/180", shown, status.crc_matches);
        canvas.text(100, 320, outcome, 5, ps5::demo::Color::white);
        std::snprintf(outcome, sizeof(outcome), "RESULT %d - CLEANUP %d", result, cleanup);
        canvas.text(100, 410, outcome, 5, ps5::demo::Color::white);
        canvas.text(100, 560, "CLIP DE 3 SEGUNDOS - DEJA LA APP ABIERTA", 4,
                    ps5::demo::Color::white);
        tube5::gpu_clear_video();
        tube5::gpu_begin_frame();
        (void)tube5::gpu_present_ui(background);
    }
    for (;;)
        sceKernelUsleep(16000);
}
