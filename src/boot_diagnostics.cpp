// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot_diagnostics.hpp"
#include <cstddef>

#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
#include <cstddef>
#include <cstdint>
extern "C" int sceKernelSendNotificationRequest(std::uint32_t, void *, std::size_t, int);
extern "C" int sceKernelUsleep(std::uint32_t);
#endif

extern "C" void tube5_boot_checkpoint(const char *message) noexcept
{
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
    // Native notification layout from the hardware-tested template. No heap,
    // stdio, memset, strlen or SDL calls before _init_env.
    struct Request
    {
        std::uint8_t reserved[45];
        char text[3075];
    } request;
    auto *bytes = reinterpret_cast<volatile unsigned char *>(&request);
    for (std::size_t i = 0; i < sizeof(request); ++i)
        bytes[i] = 0;
    std::size_t i = 0;
    if (message)
        for (; message[i] && i < sizeof(request.text) - 1; ++i)
            request.text[i] = message[i];
    request.text[i] = '\0';
    (void)sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    (void)sceKernelUsleep(700000);
#else
    (void)message;
#endif
}

void tube5_boot_result(const char *stage, std::uint32_t result) noexcept
{
    char message[160]{};
    std::size_t length = 0;
    if (stage)
        for (; stage[length] && length < sizeof(message) - 12; ++length)
            message[length] = stage[length];
    message[length++] = ' ';
    message[length++] = '0';
    message[length++] = 'x';
    constexpr char digits[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < 8; ++i)
        message[length++] = digits[(result >> (28 - i * 4)) & 0xf];
    message[length] = '\0';
    tube5_boot_checkpoint(message);
}
