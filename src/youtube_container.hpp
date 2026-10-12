// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstring>
namespace tube5
{
enum class YouTubeContainer
{
    unknown,
    mp4,
    webm
};
inline YouTubeContainer youtube_container(const void *data, std::size_t bytes) noexcept
{
    if (!data)
        return YouTubeContainer::unknown;
    const auto *p = static_cast<const unsigned char *>(data);
    if (bytes >= 8 && std::memcmp(p + 4, "ftyp", 4) == 0)
        return YouTubeContainer::mp4;
    if (bytes >= 4 && p[0] == 0x1a && p[1] == 0x45 && p[2] == 0xdf && p[3] == 0xa3)
        return YouTubeContainer::webm;
    return YouTubeContainer::unknown;
}
} // namespace tube5
