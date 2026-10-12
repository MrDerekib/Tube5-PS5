// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpu_probe.hpp"
#include <cassert>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
namespace
{
int scenario = 0, init_calls = 0, default_calls = 0;
}
static int init(unsigned version)
{
    assert(version == 8);
    ++init_calls;
    return scenario == 4 ? -17 : 0;
}
static void *defaults()
{
    ++default_calls;
    static int value;
    return scenario == 5 ? nullptr : &value;
}
extern "C" void *tube5_gpu_test_open(const char *name, int)
{
    const bool driver = std::strcmp(name, "libSceAgcDriver.sprx") == 0;
    assert(driver || std::strcmp(name, "libSceAgc.sprx") == 0);
    if ((scenario == 1 && !driver) || (scenario == 2 && driver))
        return nullptr;
    static int handles[2];
    return &handles[driver];
}
extern "C" void *tube5_gpu_test_symbol(void *, const char *name)
{
    if (scenario == 3 && std::strcmp(name, "sceAgcCreateShader") == 0)
        return nullptr;
    if (!std::strcmp(name, "sceAgcInit"))
        return reinterpret_cast<void *>(&init);
    if (!std::strcmp(name, "sceAgcGetRegisterDefaults"))
        return reinterpret_cast<void *>(&defaults);
    static int symbol;
    return &symbol; // Other exports are never called.
}
int main()
{
    for (int i = 0; i < 6; ++i)
    {
        const auto child = fork();
        assert(child >= 0);
        if (!child)
        {
            scenario = i;
            auto report = tube5::probe_gpu(nullptr);
            if (i == 0)
                assert(report.initialized && report.defaults && report.agc_exports == 0xfff &&
                       report.driver_exports == 7);
            if (i == 1 || i == 2 || i == 3)
                assert(!init_calls && !default_calls && !report.initialized);
            if (i == 4)
                assert(init_calls == 1 && !default_calls && report.init_result == -17);
            if (i == 5)
                assert(report.initialized && !report.defaults && default_calls == 1);
            _exit(0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    }
}
