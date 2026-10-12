// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpu_probe.hpp"
#include <cassert>
static int init_result = 0;
static bool has_defaults = true;
static unsigned words = 32, queried = 0;
extern "C" int sceAgcInit(unsigned version)
{
    assert(version == 8);
    return init_result;
}
extern "C" void *sceAgcGetRegisterDefaults()
{
    static int table;
    return has_defaults ? &table : nullptr;
}
extern "C" unsigned sceAgcDriverGetWaitRenderingPacketSizeInDwords()
{
    ++queried;
    return words;
}
int main()
{
    auto report = tube5::probe_gpu(nullptr);
    assert(report.initialized && report.defaults && report.wait_packet_words == 32);
    assert(report.agc_exports == 3 && report.driver_exports == 2);
    init_result = -9;
    queried = 0;
    report = tube5::probe_gpu(nullptr);
    assert(!report.initialized && report.init_result == -9 && !queried);
    init_result = 0;
    has_defaults = false;
    assert(!tube5::probe_gpu(nullptr).defaults);
    has_defaults = true;
    words = 0;
    assert(!tube5::probe_gpu(nullptr).wait_packet_words);
    words = 4097;
    assert(tube5::probe_gpu(nullptr).wait_packet_words == 4097);
}
