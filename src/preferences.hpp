// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace tube5
{
// Fixed, versioned file; no heap, JSON parser, or paths supplied by the network.
class Preferences
{
  public:
    enum class Load
    {
        loaded,
        missing,
        invalid,
        unavailable
    };
    explicit Preferences(const char *directory) noexcept
    {
        if (directory && std::strlen(directory) < sizeof(directory_) - 32)
        {
            std::snprintf(directory_, sizeof(directory_), "%s", directory);
            std::snprintf(path_, sizeof(path_), "%s/settings.conf", directory);
            std::snprintf(temporary_, sizeof(temporary_), "%s/settings.tmp", directory);
        }
    }
    Load load(bool &enabled) const noexcept
    {
        enabled = true;
        if (!path_[0])
            return Load::unavailable;
        const int fd = ::open(path_, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0)
            return errno == ENOENT ? Load::missing : Load::unavailable;
        struct stat info{};
        char text[64]{};
        const bool regular = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size < 64;
        std::size_t used = 0;
        bool okay = regular;
        while (okay && used < sizeof(text))
        {
            const auto count = ::read(fd, text + used, sizeof(text) - used);
            if (count < 0 && errno == EINTR)
                continue;
            if (count < 0)
            {
                okay = false;
                break;
            }
            if (!count)
                break;
            used += static_cast<std::size_t>(count);
        }
        ::close(fd);
        constexpr const char *on = "TUBE5_SETTINGS_V1\nSPONSORBLOCK=1\n";
        constexpr const char *off = "TUBE5_SETTINGS_V1\nSPONSORBLOCK=0\n";
        if (!okay || used != std::strlen(on))
            return Load::invalid;
        if (std::memcmp(text, on, used) == 0)
            return Load::loaded;
        if (std::memcmp(text, off, used) != 0)
            return Load::invalid;
        enabled = false;
        return Load::loaded;
    }
    bool save(bool enabled) const noexcept
    {
        if (!path_[0])
            return false;
        if (::mkdir(directory_, 0700) != 0 && errno != EEXIST)
            return false;
        struct stat info{};
        if (::lstat(directory_, &info) != 0 || !S_ISDIR(info.st_mode))
            return false;
        const int fd = ::open(temporary_, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
        if (fd < 0)
            return false;
        const char *text =
            enabled ? "TUBE5_SETTINGS_V1\nSPONSORBLOCK=1\n" : "TUBE5_SETTINGS_V1\nSPONSORBLOCK=0\n";
        const std::size_t size = std::strlen(text);
        std::size_t used = 0;
        while (used < size)
        {
            const auto count = ::write(fd, text + used, size - used);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                break;
            used += static_cast<std::size_t>(count);
        }
        bool okay = used == size && ::fsync(fd) == 0;
        if (::close(fd) != 0)
            okay = false;
        if (okay)
            okay = ::rename(temporary_, path_) == 0;
        if (!okay)
            ::unlink(temporary_);
        return okay;
    }

  private:
    char directory_[256]{}, path_[256]{}, temporary_[256]{};
};
} // namespace tube5
