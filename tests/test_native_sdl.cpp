// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_sdl.hpp"
#include <cassert>
#include <cstdint>

namespace
{
bool loaded = false;
bool becomes_loaded = true;
unsigned loads = 0;
int load_result = 0;
} // namespace
extern "C" int sceSysmoduleIsLoaded(std::uint16_t id)
{
    assert(id == 0x0106);
    return loaded ? 0 : -1;
}
extern "C" int sceSysmoduleLoadModule(std::uint16_t id)
{
    assert(id == 0x0106);
    ++loads;
    if (load_result == 0 && becomes_loaded)
        loaded = true;
    return load_result;
}
int main()
{
    assert(tube5::prepare_native_sdl() == 0 && loads == 1 && loaded);
    assert(tube5::prepare_native_sdl() == 0 && loads == 1);
    loaded = false;
    load_result = -77;
    assert(tube5::prepare_native_sdl() == -77 && loads == 2);
    load_result = 0;
    becomes_loaded = false;
    assert(tube5::prepare_native_sdl() == -1 && loads == 3);
}
