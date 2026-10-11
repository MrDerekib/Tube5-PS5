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
    explicit Preferences(const char *directory, bool prepare_directory = true) noexcept
        : prepare_directory_(prepare_directory)
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
        stage_ = "OK";
        error_ = 0;
        enabled = true;
        if (!path_[0])
        {
            fail("LOAD_PATH", EINVAL);
            return Load::unavailable;
        }
        const int fd = ::open(path_, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0)
        {
            fail("LOAD_OPEN", errno);
            return errno == ENOENT ? Load::missing : Load::unavailable;
        }
        struct stat info{};
        char text[64]{};
        const int stat_result = ::fstat(fd, &info);
        const bool regular = stat_result == 0 && S_ISREG(info.st_mode) && info.st_size < 64;
        if (!regular)
            fail("LOAD_STAT", stat_result < 0 ? errno : EINVAL);
        std::size_t used = 0;
        bool okay = regular;
        while (okay && used < sizeof(text))
        {
            const auto count = ::read(fd, text + used, sizeof(text) - used);
            if (count < 0 && errno == EINTR)
                continue;
            if (count < 0)
            {
                fail("LOAD_READ", errno);
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
        if (!okay)
            return Load::invalid;
        if (used != std::strlen(on))
        {
            fail("LOAD_FORMAT", EINVAL);
            return Load::invalid;
        }
        if (std::memcmp(text, on, used) == 0)
            return Load::loaded;
        if (std::memcmp(text, off, used) != 0)
        {
            fail("LOAD_FORMAT", EINVAL);
            return Load::invalid;
        }
        enabled = false;
        return Load::loaded;
    }
    bool save(bool enabled) const noexcept
    {
        stage_ = "OK";
        error_ = 0;
        if (!path_[0])
            return fail("PATH", EINVAL);
        // /download0 is already provided by the title's downloadDataSize.
        // Do not mkdir/lstat the mount: sandbox metadata access may be denied.
        if (prepare_directory_)
        {
            if (::mkdir(directory_, 0700) != 0 && errno != EEXIST)
                return fail("MKDIR", errno);
            struct stat info{};
            if (::lstat(directory_, &info) != 0)
                return fail("LSTAT", errno);
            if (!S_ISDIR(info.st_mode))
                return fail("DIRECTORY", ENOTDIR);
        }
        const int fd = ::open(temporary_, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
        if (fd < 0)
            return fail("OPEN", errno);
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
            {
                fail("WRITE", count < 0 ? errno : EIO);
                break;
            }
            used += static_cast<std::size_t>(count);
        }
        bool okay = used == size;
        if (okay && ::fsync(fd) != 0)
            okay = fail("SYNC", errno);
        if (::close(fd) != 0)
        {
            if (okay)
                fail("CLOSE", errno);
            okay = false;
        }
        if (okay && ::rename(temporary_, path_) != 0)
            okay = fail("RENAME", errno);
        if (!okay)
            ::unlink(temporary_);
        return okay;
    }
    const char *stage() const noexcept
    {
        return stage_;
    }
    int error() const noexcept
    {
        return error_;
    }

  private:
    bool fail(const char *stage, int error) const noexcept
    {
        stage_ = stage;
        error_ = error;
        return false;
    }
    bool prepare_directory_ = true;
    mutable const char *stage_ = "LOAD";
    mutable int error_ = 0;
    char directory_[256]{}, path_[256]{}, temporary_[256]{};
};
} // namespace tube5
