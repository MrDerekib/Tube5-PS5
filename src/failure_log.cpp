// SPDX-License-Identifier: GPL-3.0-or-later
#include "failure_log.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace tube5
{
namespace
{
char build[41] = "UNKNOWN";
}
void failure_log_build_label(const char *label) noexcept
{
    if (!label || !*label)
        return;
    std::size_t i = 0;
    for (; label[i] && i < sizeof(build) - 1; ++i)
    {
        const char c = label[i];
        build[i] = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                           c == ' ' || c == '.'
                       ? c
                       : '_';
    }
    build[i] = 0;
}
const char *failure_log_build_label() noexcept
{
    return build;
}
int write_failure_log(const char *path, const char *report, std::size_t size) noexcept
{
    if (!path || !report || !size || size > 4096 || std::strlen(path) > 190)
        return EINVAL;
    char temporary[200]{};
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    const int fd = ::open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return errno;
    std::size_t used = 0;
    int error = 0;
    while (used < size)
    {
        const auto count = ::write(fd, report + used, size - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            error = count < 0 ? errno : EIO;
            break;
        }
        used += static_cast<std::size_t>(count);
    }
    if (!error && ::fsync(fd) != 0)
        error = errno;
    // Numeric-only snapshots are readable by the separate FTP payload UID.
    // Keep the incomplete temporary private; never relax settings/token files.
    if (!error && ::fchmod(fd, 0644) != 0)
        error = errno;
    if (::close(fd) != 0 && !error)
        error = errno;
    if (!error && ::rename(temporary, path) != 0)
        error = errno;
    if (error)
        (void)::unlink(temporary);
    return error;
}
} // namespace tube5
