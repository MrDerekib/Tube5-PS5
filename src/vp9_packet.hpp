// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace tube5
{
struct Vp9Part
{
    const std::uint8_t *data = nullptr;
    std::size_t bytes = 0;
    bool displayable = false, existing = false;
};
struct Vp9Packet
{
    std::array<Vp9Part, 8> parts{};
    unsigned count = 0;
};
// Profile 0 only. Validate the complete superframe index before exposing parts.
inline bool split_vp9_packet(const std::uint8_t *data, std::size_t bytes, Vp9Packet &out) noexcept
{
    out = {};
    if (!data || !bytes)
        return false;
    Vp9Packet parsed{};
    const auto marker = data[bytes - 1];
    const bool compound = (marker & 0xe0u) == 0xc0u;
    const unsigned count = compound ? (marker & 7u) + 1 : 1;
    const unsigned magnitude = ((marker >> 3) & 3u) + 1;
    const std::size_t index_bytes = compound ? 2 + count * magnitude : 0;
    if (index_bytes >= bytes || (compound && data[bytes - index_bytes] != marker))
        return false;
    const auto payload = bytes - index_bytes;
    std::size_t offset = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        std::size_t size = compound ? 0 : bytes;
        if (compound)
            for (unsigned j = 0; j < magnitude; ++j)
                size |= static_cast<std::size_t>(data[payload + 1 + i * magnitude + j]) << (j * 8);
        if (!size || size > payload - offset)
            return false;
        const auto first = data[offset];
        if ((first & 0xf0u) != 0x80u) // frame marker 2, profile bits both zero
            return false;
        const bool existing = (first & 8u) != 0;
        parsed.parts[i] = {data + offset, size, existing || (first & 2u) != 0, existing};
        offset += size;
    }
    if (offset != payload)
        return false;
    parsed.count = count;
    out = parsed;
    return true;
}
} // namespace tube5
