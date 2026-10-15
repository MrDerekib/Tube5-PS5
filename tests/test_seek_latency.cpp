// SPDX-License-Identifier: GPL-3.0-or-later
#include "seek_latency.hpp"
#include "failure_log.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
int main()
{
    tube5::SeekDecodeTimer timer;
    timer.begin(1000, 7);
    timer.packet(1100); // Old generation must not count as post-reset input.
    timer.reset(1200, 7, 100, 5000);
    timer.skip();
    assert(!timer.snapshot().reset && !timer.snapshot().packet && !timer.snapshot().skipped);
    timer.reset(1300, 8, 103, 5100);
    timer.packet(1400);
    timer.packet(1500);
    timer.skip(); timer.skip();
    timer.decoded(1900, 110, 5600);
    timer.decoded(2000, 999, 9999); // First target only, no later overwrite.
    auto s = timer.snapshot();
    assert(s.prepared == 1000 && s.reset == 1300 && s.packet == 1400 && s.decoded == 1900);
    assert(s.skipped == 2 && s.calls == 7 && s.decode_us == 500);
    timer.begin(3000, 8);
    assert(!timer.snapshot().decoded && !timer.snapshot().skipped);
    timer.cancel();
    timer.reset(3100, 9, 110, 5600);
    timer.packet(3200);
    assert(!timer.snapshot().reset && !timer.snapshot().packet);
    timer.begin(4000, UINT32_MAX);
    timer.reset(4100, 0, 100, 1000); // Reset generation wrap still counts.
    assert(timer.snapshot().reset == 4100);
    tube5::SeekLatencyHistory history;
    history.begin(800, 900, 5000);
    auto *r = history.current();
    r->native = s;
    r->returned = 1250;
    r->presented = 2100;
    r->audio = 2200;
    r->status = 1;
    char report[8192];
    auto n = history.format(report, sizeof(report), "TEST");
    assert(std::strstr(report, "1 5000 1 100 100 350 400 500 1000 1200 1300 2 7 500\n"));
    history.begin(2300, 2400, 6000);
    history.begin(2500, 2600, 7000);
    assert(history.records[1].status == 3); // Superseded, never reported complete.
    for (unsigned i = 0; i < 20; ++i)
        history.begin(3000 + i, 4000 + i, 8000 + i);
    n = history.format(report, sizeof(report), "TEST");
    assert(n == std::strlen(report) && n < sizeof(report) - 1);
    assert(std::strstr(report, "TOTAL 23\n") && std::strstr(report, "23 8019 0 "));
    assert(!std::strstr(report, "1 5000 1 "));
    char small[8];
    assert(history.format(small, sizeof(small), "TEST") == 7 && !small[7]);
    assert(!history.format(nullptr, 0, "TEST"));
    char path[] = "/tmp/tube5-seek-latency-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && !close(fd));
    assert(!tube5::write_trace_log(path, report, n));
    FILE *saved = std::fopen(path, "rb");
    assert(saved);
    char loaded[8192];
    assert(std::fread(loaded, 1, sizeof(loaded), saved) == n);
    assert(!std::memcmp(loaded, report, n) && !std::fclose(saved));
    assert(!unlink(path));
    std::puts("Seek phases: reset generations, first packet/frame, counters, cancel, coalescing, ring and disk passed");
}
