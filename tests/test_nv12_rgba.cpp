// SPDX-License-Identifier: GPL-3.0-or-later
#include "nv12_rgba.hpp"
#include <array>
#include <cassert>
int main()
{
    constexpr unsigned width = 4, height = 2, surface_height = 2, pitch = 4;
    std::array<std::uint8_t, pitch * surface_height * 3 / 2> nv12{16,  16,  235, 235, 16,  16,
                                                                  235, 235, 128, 128, 128, 128};
    std::array<std::uint8_t, width * height * 4> rgba{};
    assert(tube5::nv12_to_rgba(nv12.data(), nv12.size(), width, height, surface_height, pitch,
                               rgba.data(), width * 4));
    for (unsigned pixel = 0; pixel < width * height; ++pixel)
    {
        const unsigned source_x = pixel % width;
        const std::uint8_t expected = source_x < 2 ? 0 : 255;
        assert(rgba[pixel * 4] == expected && rgba[pixel * 4 + 1] == expected &&
               rgba[pixel * 4 + 2] == expected && rgba[pixel * 4 + 3] == 255);
    }
    nv12[8] = 102; // BT.709 red chroma.
    nv12[9] = 240;
    nv12[0] = nv12[1] = nv12[4] = nv12[5] = 63;
    assert(tube5::nv12_to_rgba(nv12.data(), nv12.size(), width, height, surface_height, pitch,
                               rgba.data(), width * 4));
    assert(rgba[0] > 240 && rgba[1] < 20 && rgba[2] < 20 && rgba[3] == 255);
    assert(!tube5::nv12_to_rgba(nv12.data(), nv12.size() - 1, width, height, surface_height, pitch,
                                rgba.data(), width * 4));
    assert(!tube5::nv12_to_rgba(nv12.data(), nv12.size(), width, height, surface_height, 3,
                                rgba.data(), width * 4));
}
