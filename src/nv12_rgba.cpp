// SPDX-License-Identifier: GPL-3.0-or-later
#include "nv12_rgba.hpp"
#include <algorithm>
#include <limits>
namespace tube5
{
bool nv12_to_rgba(const std::uint8_t *source, std::size_t source_bytes, unsigned width,
                  unsigned height, unsigned surface_height, unsigned source_pitch,
                  std::uint8_t *destination, std::size_t destination_pitch) noexcept
{
    if (!source || !destination || !width || !height || (width & 1u) || (height & 1u) ||
        surface_height < height || (surface_height & 1u) || source_pitch < width ||
        (source_pitch & 1u) || destination_pitch < static_cast<std::size_t>(width) * 4)
        return false;
    const std::uint64_t total_rows = static_cast<std::uint64_t>(surface_height) + surface_height / 2;
    if (total_rows > std::numeric_limits<std::uint64_t>::max() / source_pitch ||
        destination_pitch > std::numeric_limits<std::size_t>::max() / height)
        return false;
    const std::uint64_t y_bytes = static_cast<std::uint64_t>(source_pitch) * surface_height;
    const std::uint64_t total_bytes = static_cast<std::uint64_t>(source_pitch) * total_rows;
    if (total_bytes > source_bytes || total_bytes > std::numeric_limits<std::size_t>::max())
        return false;
    for (unsigned y = 0; y < height; ++y)
    {
        const auto *luma = source + static_cast<std::size_t>(y) * source_pitch;
        const auto *chroma = source + static_cast<std::size_t>(y_bytes) +
                             static_cast<std::size_t>(y / 2) * source_pitch;
        auto *rgba = destination + static_cast<std::size_t>(y) * destination_pitch;
        for (unsigned x = 0; x < width; ++x)
        {
            const int c = std::max(0, static_cast<int>(luma[x]) - 16);
            const int d = static_cast<int>(chroma[x & ~1u]) - 128;
            const int e = static_cast<int>(chroma[(x & ~1u) + 1]) - 128;
            rgba[x * 4] =
                static_cast<std::uint8_t>(std::clamp((298 * c + 459 * e + 128) >> 8, 0, 255));
            rgba[x * 4 + 1] = static_cast<std::uint8_t>(
                std::clamp((298 * c - 55 * d - 136 * e + 128) >> 8, 0, 255));
            rgba[x * 4 + 2] =
                static_cast<std::uint8_t>(std::clamp((298 * c + 541 * d + 128) >> 8, 0, 255));
            rgba[x * 4 + 3] = 255;
        }
    }
    return true;
}
} // namespace tube5
