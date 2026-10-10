// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_sdl.hpp"
#include "boot_diagnostics.hpp"
#include <cstdint>

#ifndef TUBE5_HOST
extern "C" int sceSysmoduleIsLoaded(std::uint16_t);
extern "C" int sceSysmoduleLoadModule(std::uint16_t);
#endif

namespace tube5
{
int prepare_native_sdl() noexcept
{
#ifdef TUBE5_HOST
    return 0;
#else
    constexpr std::uint16_t keyboard_module = 0x0106;
    tube5_boot_checkpoint("Tube5 D04K: cargar modulo Keyboard");
    // IsLoaded returns zero for loaded, not a C boolean.
    if (sceSysmoduleIsLoaded(keyboard_module) == 0)
        return 0;
    const int result = sceSysmoduleLoadModule(keyboard_module);
    tube5_boot_result("Tube5 D04K: carga Keyboard", static_cast<std::uint32_t>(result));
    if (result != 0)
        return result;
    const int loaded = sceSysmoduleIsLoaded(keyboard_module);
    tube5_boot_result("Tube5 D04K: comprobar Keyboard", static_cast<std::uint32_t>(loaded));
    return loaded;
#endif
}
} // namespace tube5

#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
// Opt-in link wrappers around calls in the *pinned* PacBrew archive, without
// replacing their behavior or rebuilding SDL against a different source tip.
extern "C" int __real_PS5_Keyboard_Init();
extern "C" int __real_PS5_Keyboard_Open();
extern "C" int __real_sceVideoOutOpen(std::int32_t, std::int32_t, std::int32_t, const void *);

extern "C" int __wrap_PS5_Keyboard_Init()
{
    tube5_boot_checkpoint("Tube5 D05K1: SDL Keyboard Init");
    const int result = __real_PS5_Keyboard_Init();
    tube5_boot_result("Tube5 D05K1: Keyboard Init retorno", static_cast<std::uint32_t>(result));
    return result;
}
extern "C" int __wrap_PS5_Keyboard_Open()
{
    tube5_boot_checkpoint("Tube5 D05K2: SDL Keyboard Open");
    const int result = __real_PS5_Keyboard_Open();
    tube5_boot_result("Tube5 D05K2: Keyboard Open retorno", static_cast<std::uint32_t>(result));
    return result;
}
extern "C" int __wrap_sceVideoOutOpen(std::int32_t user, std::int32_t bus, std::int32_t index,
                                      const void *parameter)
{
    tube5_boot_checkpoint("Tube5 D05V: abrir VideoOut");
    const int result = __real_sceVideoOutOpen(user, bus, index, parameter);
    tube5_boot_result("Tube5 D05V: VideoOut retorno", static_cast<std::uint32_t>(result));
    return result;
}
#endif
