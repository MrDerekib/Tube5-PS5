// SPDX-License-Identifier: GPL-3.0-or-later
#include "vp9_output_trace.hpp"
#include "failure_log.hpp"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
int main()
{
    tube5::Vp9OutputTrace trace;
    for (unsigned i = 1; i <= 80; ++i)
    {
        tube5::Vp9OutputEvent event{};
        event.submitted = i;
        event.signature = i;
        event.pending[0] = {i, UINT64_MAX, i * 1000, false, true};
        event.owners[0] = i;
        auto &stored = trace.push(event);
        stored.policy = 5;
    }
    char report[65536];
    const auto n = trace.format(report, sizeof(report), "TEST", "POLICY", -22);
    assert(n == std::strlen(report) && n < sizeof(report) - 1);
    assert(std::strstr(report, "TOTAL 80\n"));
    assert(std::strstr(report, "EVENT 17 IN 17 "));
    assert(!std::strstr(report, "EVENT 16 IN 16 "));
    assert(std::strstr(report, "EVENT 80 IN 80 "));
    assert(std::strstr(report, "POLICY 5\n"));
    assert(std::strstr(report, "P 0:80/18446744073709551615/80000/0/1\n"));
    // DIAG107 regression: formatter passed, but the 4 KiB snapshot writer
    // rejected the history. Verify the actual atomic file, not just formatting.
    assert(n > 4096);
    char path[] = "/tmp/tube5-vp9-trace-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0 && !close(fd));
    assert(tube5::write_failure_log(path, report, n) == EINVAL);
    assert(!tube5::write_trace_log(path, report, n));
    struct stat info{};
    assert(!stat(path, &info) && static_cast<std::size_t>(info.st_size) == n);
    assert((info.st_mode & 0777) == 0644);
    FILE *saved = std::fopen(path, "rb");
    assert(saved);
    char loaded[65536];
    assert(std::fread(loaded, 1, sizeof(loaded), saved) == n);
    assert(!std::memcmp(report, loaded, n) && !std::fclose(saved));
    assert(tube5::write_trace_log(path, report, 65537) == EINVAL);
    assert(!unlink(path));
    struct { char small[15]; char guard[8]; } bounded{};
    std::memset(bounded.guard, 'X', sizeof(bounded.guard));
    assert(trace.format(bounded.small, sizeof(bounded.small), "TEST", "FAIL", -1) == 14);
    assert(bounded.small[14] == 0);
    for (auto c : bounded.guard) assert(c == 'X');
    assert(trace.format(nullptr, 0, "TEST", "FAIL", -1) == 0);
    std::puts("VP9 trace: ring, bounds, atomic file roundtrip >4 KiB, permissions and 64 KiB limit passed");
}
