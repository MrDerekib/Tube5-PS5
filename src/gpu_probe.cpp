// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpu_probe.hpp"
#include "failure_log.hpp"
#include <algorithm>
#include <cstdio>
#ifdef TUBE5_GPU_PROBE
#include <dlfcn.h>
#ifdef TUBE5_GPU_PROBE_MOCK
extern "C" void *tube5_gpu_test_open(const char *, int);
extern "C" void *tube5_gpu_test_symbol(void *, const char *);
#define GPU_OPEN tube5_gpu_test_open
#define GPU_SYMBOL tube5_gpu_test_symbol
#else
#define GPU_OPEN dlopen
#define GPU_SYMBOL dlsym
#endif
#endif
namespace tube5
{
GpuProbeReport probe_gpu(const char *log_path) noexcept
{
    GpuProbeReport report;
#ifdef TUBE5_GPU_PROBE
    const auto save = [&](const char *stage)
    {
        char text[512]{};
        const int length = std::snprintf(
            text, sizeof(text),
            "TUBE5 GPU ACCESS V1\nBUILD %s\nSTAGE %s\nAGC LOAD %u DRIVER LOAD %u\n"
            "AGC EXPORTS %08X DRIVER EXPORTS %08X\nINIT VERSION 8 RESULT %08X OK %u DEFAULTS %u\n"
            "GPU SUBMISSIONS 0 - PLAYER PRESENTATION STILL CPU\n",
            failure_log_build_label(), stage, report.agc_loaded ? 1u : 0u,
            report.driver_loaded ? 1u : 0u, report.agc_exports, report.driver_exports,
            static_cast<unsigned>(report.init_result), report.initialized ? 1u : 0u,
            report.defaults ? 1u : 0u);
        if (length > 0)
            (void)write_failure_log(log_path, text,
                                    std::min<std::size_t>(length, sizeof(text) - 1));
    };
    // Process-owned references: AGC's initialized globals must not point into
    // an unloaded module. No Sony module binaries are redistributed.
    static void *agc = nullptr, *driver = nullptr;
    save("LOAD AGC");
    if (!agc)
        agc = GPU_OPEN("libSceAgc.sprx", RTLD_NOW | RTLD_LOCAL);
    report.agc_loaded = agc != nullptr;
    save("LOAD DRIVER");
    if (!driver)
        driver = GPU_OPEN("libSceAgcDriver.sprx", RTLD_NOW | RTLD_LOCAL);
    report.driver_loaded = driver != nullptr;
    if (!agc || !driver)
    {
        save("MODULE UNAVAILABLE");
        return report;
    }
    constexpr const char *agc_names[] = {"sceAgcInit",
                                         "sceAgcGetRegisterDefaults",
                                         "sceAgcCreateShader",
                                         "sceAgcLinkShaders",
                                         "sceAgcDcbSetCxRegistersIndirect",
                                         "sceAgcDcbSetShRegistersIndirect",
                                         "sceAgcDcbSetUcRegistersIndirect",
                                         "sceAgcCbSetShRegisterRangeDirect",
                                         "sceAgcDcbDrawIndexAuto",
                                         "sceAgcDcbSetFlip",
                                         "sceAgcCbReleaseMem",
                                         "sceAgcSuspendPoint"};
    constexpr const char *driver_names[] = {"sceAgcDriverSubmitDcb",
                                            "sceAgcDriverGetWaitRenderingPacketSizeInDwords",
                                            "sceAgcDriverWaitUntilSafeForRendering"};
    void *init_address = nullptr, *defaults_address = nullptr;
    for (unsigned i = 0; i < 12; ++i)
    {
        void *address = GPU_SYMBOL(agc, agc_names[i]);
        if (address)
            report.agc_exports |= 1u << i;
        if (i == 0)
            init_address = address;
        if (i == 1)
            defaults_address = address;
    }
    for (unsigned i = 0; i < 3; ++i)
        if (GPU_SYMBOL(driver, driver_names[i]))
            report.driver_exports |= 1u << i;
    if (report.agc_exports != 0xfffu || report.driver_exports != 7u)
    {
        save("EXPORT UNAVAILABLE");
        return report;
    }
    save("INIT AGC");
    // Public native presenter ABI in pinned ProsperoLight and PS5_Vulkan:
    // sceAgcInit(uint32_t version), NOT the historical state/size prototype.
    report.init_result = reinterpret_cast<int (*)(std::uint32_t)>(init_address)(8);
    report.initialized = report.init_result == 0;
    if (!report.initialized)
    {
        save("INIT FAILED");
        return report;
    }
    save("QUERY DEFAULTS");
    report.defaults = reinterpret_cast<void *(*)()>(defaults_address)() != nullptr;
    save(report.defaults ? "ACCESS READY - NO GPU DRAW" : "DEFAULTS UNAVAILABLE");
#else
    (void)log_path;
#endif
    return report;
}
} // namespace tube5
