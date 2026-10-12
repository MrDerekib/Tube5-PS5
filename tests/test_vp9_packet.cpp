// SPDX-License-Identifier: GPL-3.0-or-later
#include "vp9_packet.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
int main(int argc, char **argv)
{
    tube5::Vp9Packet packet;
    const std::uint8_t visible[]{0x82, 0};
    assert(tube5::split_vp9_packet(visible, sizeof(visible), packet));
    assert(packet.count == 1 && packet.parts[0].displayable && !packet.parts[0].existing);
    const std::uint8_t compound[]{0x84, 0, 0x86, 0, 0xc1, 2, 2, 0xc1};
    assert(tube5::split_vp9_packet(compound, sizeof(compound), packet));
    assert(packet.count == 2 && !packet.parts[0].displayable && packet.parts[1].displayable);
    const std::uint8_t existing[]{0x88};
    assert(tube5::split_vp9_packet(existing, 1, packet) && packet.parts[0].existing);
    auto broken = std::array<std::uint8_t, 8>{0x84, 0, 0x86, 0, 0xc1, 3, 2, 0xc1};
    assert(!tube5::split_vp9_packet(broken.data(), broken.size(), packet) && packet.count == 0);
    broken[5] = 0;
    assert(!tube5::split_vp9_packet(broken.data(), broken.size(), packet));
    broken[5] = 2;
    broken[4] = 0xc0;
    assert(!tube5::split_vp9_packet(broken.data(), broken.size(), packet));
    assert(!tube5::split_vp9_packet(compound + 7, 1, packet));
    const std::uint8_t other_profile[]{0xa2};
    assert(!tube5::split_vp9_packet(other_profile, 1, packet));
    assert(!tube5::split_vp9_packet(nullptr, 0, packet));
    if (argc == 2)
    {
        FILE *file = std::fopen(argv[1], "rb");
        assert(file);
        std::uint32_t header[7]{};
        assert(std::fread(header, sizeof(header), 1, file) == 1 && header[1] == 4 &&
               header[6] == 2);
        unsigned hidden = 0, compound_count = 0, visible = 0;
        for (unsigned i = 0; i < header[5]; ++i)
        {
            std::uint32_t entry[4]{};
            assert(std::fread(entry, sizeof(entry), 1, file) == 1 && entry[1] == i &&
                   entry[2] == i);
            std::vector<std::uint8_t> bytes(entry[0]);
            assert(std::fread(bytes.data(), bytes.size(), 1, file) == 1);
            assert(tube5::split_vp9_packet(bytes.data(), bytes.size(), packet));
            compound_count += packet.count > 1;
            unsigned packet_visible = 0;
            for (unsigned j = 0; j < packet.count; ++j)
            {
                hidden += !packet.parts[j].displayable;
                packet_visible += packet.parts[j].displayable;
            }
            assert(packet_visible == 1 && packet.parts[packet.count - 1].displayable);
            visible += packet_visible;
        }
        assert(std::fgetc(file) == EOF && hidden > 0 && compound_count > 0 && visible == 90);
        std::fclose(file);
        std::printf("VP9 parser: %u visible, %u hidden, %u superframes validated\n", visible,
                    hidden, compound_count);
    }
}
