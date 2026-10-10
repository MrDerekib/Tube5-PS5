// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

extern "C" SDL_Surface *__wrap_SDL_CreateRGBSurface_REAL(Uint32, int, int, int, Uint32, Uint32,
                                                         Uint32, Uint32);

namespace
{
bool fail = false;
SDL_Surface result{};
std::vector<std::string> messages;
std::vector<Uint32> values;
} // namespace

extern "C" SDL_Surface *__real_SDL_CreateRGBSurface_REAL(Uint32 flags, int width, int height,
                                                         int depth, Uint32 red, Uint32 green,
                                                         Uint32 blue, Uint32 alpha)
{
    assert(flags == 7 && width == 640 && height == 360 && depth == 32);
    assert(red == 0xff && green == 0xff00 && blue == 0xff0000 && alpha == 0xff000000);
    if (fail)
    {
        SDL_SetError("injected allocation failure");
        return nullptr;
    }
    return &result;
}
extern "C" void tube5_boot_checkpoint(const char *message) noexcept
{
    messages.emplace_back(message);
}
void tube5_boot_result(const char *, std::uint32_t value) noexcept
{
    values.push_back(value);
}

int main()
{
    const auto create = []
    {
        return __wrap_SDL_CreateRGBSurface_REAL(7, 640, 360, 32, 0xff, 0xff00, 0xff0000,
                                                0xff000000);
    };
    assert(create() == &result && messages.empty() && values.empty());
    fail = true;
    assert(create() == nullptr);
    // Simulate SW_CreateTexture overwriting the error after the constructor.
    SDL_SetError("Parameter 'surface' is invalid");
    assert(messages.size() == 2 && messages[1] == "injected allocation failure");
    assert((values == std::vector<Uint32>{640, 360, 32}));
    SDL_ClearError();
    SDL_Quit();
}
