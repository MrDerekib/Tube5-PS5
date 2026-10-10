// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cassert>
#include <cstddef>
#include <vector>

namespace
{
SDL_malloc_func original_malloc;
std::size_t denied_size = 0;
unsigned denied_calls = 0;
void *checked_malloc(std::size_t size)
{
    if (denied_size && size == denied_size)
    {
        ++denied_calls;
        return nullptr;
    }
    return original_malloc(size);
}
} // namespace

int main()
{
    SDL_calloc_func original_calloc;
    SDL_realloc_func original_realloc;
    SDL_free_func original_free;
    SDL_GetMemoryFunctions(&original_malloc, &original_calloc, &original_realloc, &original_free);
    assert(SDL_SetMemoryFunctions(checked_malloc, original_calloc, original_realloc,
                                  original_free) == 0);
    SDL_SetMainReady();
    assert(SDL_Init(0) == 0);
    auto *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 360, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    auto *renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);
    for (const auto size : {SDL_Point{640, 360}, SDL_Point{1280, 720}, SDL_Point{1920, 1080}})
    {
        std::vector<Uint8> y(size.x * size.y, 82), u(size.x * size.y / 4, 90),
            v(size.x * size.y / 4, 240);
        auto *static_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV,
                                                 SDL_TEXTUREACCESS_STATIC, size.x, size.y);
        auto *streaming = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV,
                                            SDL_TEXTUREACCESS_STREAMING, size.x, size.y);
        assert(static_texture && streaming);
        denied_size = static_cast<std::size_t>(size.x) * size.y * 4;
        denied_calls = 0;
        assert(SDL_UpdateYUVTexture(static_texture, nullptr, y.data(), size.x, u.data(), size.x / 2,
                                    v.data(), size.x / 2) < 0);
        assert(denied_calls == 1);
        SDL_ClearError();
        denied_calls = 0;
        assert(SDL_UpdateYUVTexture(streaming, nullptr, y.data(), size.x, u.data(), size.x / 2,
                                    v.data(), size.x / 2) == 0);
        assert(denied_calls == 0);
        denied_size = 0;
        assert(SDL_RenderCopy(renderer, streaming, nullptr, nullptr) == 0);
        SDL_RenderPresent(renderer);
        assert(SDL_LockSurface(surface) == 0);
        Uint8 red, green, blue;
        SDL_GetRGB(*static_cast<Uint32 *>(surface->pixels), surface->format, &red, &green, &blue);
        assert(red > 150 && green < 100 && blue < 100);
        SDL_UnlockSurface(surface);
        SDL_DestroyTexture(static_texture);
        SDL_DestroyTexture(streaming);
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
}
