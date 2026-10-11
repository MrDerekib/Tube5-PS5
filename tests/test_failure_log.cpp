// SPDX-License-Identifier: GPL-3.0-or-later
#include "failure_log.hpp"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
int main()
{
    char directory[] = "/tmp/tube5-log-XXXXXX";
    assert(mkdtemp(directory));
    char path[200]{}, temporary[210]{};
    std::snprintf(path, sizeof(path), "%s/debug.log", directory);
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    assert(!tube5::write_failure_log(path, "FIRST\n", 6));
    assert(!tube5::write_failure_log(path, "SECOND\n", 7));
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    struct stat metadata{};
    assert(fstat(fd, &metadata) == 0 && (metadata.st_mode & 0777) == 0644);
    char text[64]{};
    assert(read(fd, text, sizeof(text)) == 7);
    assert(!std::strcmp(text, "SECOND\n"));
    assert(close(fd) == 0);
    assert(tube5::write_failure_log(path, text, 4097) == EINVAL);
    assert(tube5::write_failure_log(nullptr, text, 1) == EINVAL);
    assert(tube5::write_failure_log("/nonexistent-tube5/report", text, 1) != 0);
    assert(symlink(path, temporary) == 0);
    assert(tube5::write_failure_log(path, "BAD", 3) != 0);
    assert(unlink(temporary) == 0);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    tube5::failure_log_build_label("BOOT-DIAG-52-MOVINDEX");
    assert(!std::strcmp(tube5::failure_log_build_label(), "BOOT-DIAG-52-MOVINDEX"));
    tube5::failure_log_build_label("A/B\nC");
    assert(!std::strcmp(tube5::failure_log_build_label(), "A_B_C"));
}
