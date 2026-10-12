// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace tube5
{
// Convert the 8-bit limited-range BT.709 NV12 surface to byte-ordered RGBA.
bool nv12_to_rgba(const std::uint8_t *source, std::size_t source_bytes, unsigned width,
                  unsigned height, unsigned surface_height, unsigned source_pitch,
                  std::uint8_t *destination, std::size_t destination_pitch) noexcept;
} // namespace tube5
