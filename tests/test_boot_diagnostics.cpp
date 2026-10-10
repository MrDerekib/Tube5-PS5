// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot_diagnostics.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
std::array<unsigned char, 3120> captured{};
unsigned waits = 0;
} // namespace

extern "C" int sceKernelSendNotificationRequest(std::uint32_t device, void *request,
                                                std::size_t size, int blocking)
{
    assert(device == 0 && blocking == 0 && size == captured.size());
    std::memcpy(captured.data(), request, size);
    return 0;
}
extern "C" int sceKernelUsleep(std::uint32_t delay)
{
    assert(delay == 700000);
    ++waits;
    return 0;
}

int main()
{
    tube5_boot_checkpoint("Tube5 D00");
    assert(std::strcmp(reinterpret_cast<const char *>(captured.data() + 45), "Tube5 D00") == 0);
    for (std::size_t i = 0; i < 45; ++i)
        assert(captured[i] == 0);
    std::array<char, 4000> long_message{};
    long_message.fill('A');
    long_message.back() = '\0';
    tube5_boot_checkpoint(long_message.data());
    assert(captured[45] == 'A' && captured.back() == 0);
    tube5_boot_checkpoint(nullptr);
    assert(captured[45] == 0 && waits == 3);
}
