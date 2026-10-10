// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "native_surface_buffers.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" void __wrap_SDL_FreeSurface_REAL(SDL_Surface *);
extern "C" SDL_Surface *__real_SDL_CreateRGBSurfaceFrom(void *, int, int, int, int, Uint32, Uint32,
                                                        Uint32, Uint32);
namespace
{
constexpr std::size_t maximum = 8 * 1024 * 1024;
alignas(0x4000) std::uint8_t memory[4][maximum];
std::array<bool, 4> allocated{}, mapped{};
std::array<std::size_t, 4> lengths{};
int allocation_error = 0, map_error = 0, unmap_error = 0;
bool metadata_fail = false, original_fail = false;
unsigned original_calls = 0, frees = 0, releases = 0;
unsigned index(std::int64_t physical)
{
    assert(physical >= 0 && physical % maximum == 0);
    const auto result = static_cast<unsigned>(physical / maximum);
    assert(result < 4);
    return result;
}
void expect_live(unsigned value)
{
    char text[128]{}, prefix[32]{};
    tube5::native_surface_buffer_status(text, sizeof(text));
    std::snprintf(prefix, sizeof(prefix), "SBUF %u/4 ", value);
    assert(std::strncmp(text, prefix, std::strlen(prefix)) == 0);
}
SDL_Surface *create(int width = 1920, int height = 1080, int depth = 32)
{
    return tube5::create_texture_surface(0, width, height, depth, 0xff, 0xff00, 0xff0000,
                                         depth == 32 ? 0xff000000 : 0);
}
} // namespace
extern "C" std::size_t sceKernelGetDirectMemorySize()
{
    return 4 * maximum;
}
extern "C" int sceKernelAllocateDirectMemory(std::int64_t start, std::int64_t end,
                                             std::size_t bytes, std::size_t alignment, int type,
                                             std::int64_t *physical)
{
    assert(start == 0 && end == static_cast<std::int64_t>(4 * maximum) && bytes <= maximum &&
           bytes % 0x4000 == 0 && alignment == 0x4000 && type == 12);
    if (allocation_error)
        return allocation_error;
    for (unsigned i = 0; i < 4; ++i)
        if (!allocated[i])
        {
            allocated[i] = true;
            lengths[i] = bytes;
            *physical = i * maximum;
            return 0;
        }
    return -12;
}
extern "C" int sceKernelMapDirectMemory(void **address, std::size_t bytes, int protection,
                                        int flags, std::int64_t physical, std::size_t alignment)
{
    const unsigned i = index(physical);
    assert(allocated[i] && !mapped[i] && bytes == lengths[i] && protection == 0x33 && flags == 0 &&
           alignment == 0x4000);
    if (map_error)
        return map_error;
    mapped[i] = true;
    *address = memory[i];
    return 0;
}
extern "C" int sceKernelReleaseDirectMemory(std::int64_t physical, std::size_t bytes)
{
    const unsigned i = index(physical);
    assert(allocated[i] && !mapped[i] && bytes == lengths[i]);
    allocated[i] = false;
    ++releases;
    return 0;
}
extern "C" int sceKernelMunmap(void *address, std::size_t bytes)
{
    for (unsigned i = 0; i < 4; ++i)
        if (address == memory[i])
        {
            assert(mapped[i] && bytes == lengths[i]);
            if (unmap_error)
                return unmap_error;
            mapped[i] = false;
            return 0;
        }
    assert(false);
    return -22;
}
extern "C" SDL_Surface *__real_SDL_CreateRGBSurface_REAL(Uint32 flags, int width, int height,
                                                         int depth, Uint32 red, Uint32 green,
                                                         Uint32 blue, Uint32 alpha)
{
    ++original_calls;
    if (original_fail)
    {
        SDL_SetError("injected original pixel allocation failure");
        return nullptr;
    }
    return SDL_CreateRGBSurface(flags, width, height, depth, red, green, blue, alpha);
}
extern "C" void __real_SDL_FreeSurface_REAL(SDL_Surface *surface)
{
    ++frees;
    SDL_FreeSurface(surface);
}
extern "C" SDL_Surface *__wrap_SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height,
                                                        int depth, int pitch, Uint32 red,
                                                        Uint32 green, Uint32 blue, Uint32 alpha)
{
    if (metadata_fail)
    {
        SDL_SetError("injected metadata failure");
        return nullptr;
    }
    return __real_SDL_CreateRGBSurfaceFrom(pixels, width, height, depth, pitch, red, green, blue,
                                           alpha);
}
int main()
{
    SDL_SetMainReady();
    assert(SDL_Init(0) == 0);
    original_fail = true; // Pixel storage still works if the old allocator fails.
    SDL_Surface *surface = create();
    assert(surface && original_calls == 0 && surface->pixels == memory[0]);
    assert(surface->w == 1920 && surface->h == 1080 && surface->pitch == 1920 * 4 &&
           (surface->flags & SDL_PREALLOC));
    expect_live(1);
    assert(SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 220, 30, 10, 255)) == 0);
    Uint8 red, green, blue;
    SDL_GetRGB(*static_cast<Uint32 *>(surface->pixels), surface->format, &red, &green, &blue);
    assert(red == 220 && green == 30 && blue == 10);
    ++surface->refcount;
    __wrap_SDL_FreeSurface_REAL(surface);
    expect_live(1);
    assert(mapped[0] && releases == 0 && surface->refcount == 1);
    __wrap_SDL_FreeSurface_REAL(surface);
    expect_live(0);
    assert(releases == 1 && !mapped[0]);
    surface = create(1280, 720);
    assert(surface && *static_cast<Uint32 *>(surface->pixels) == 0);
    __wrap_SDL_FreeSurface_REAL(surface);
    metadata_fail = true;
    const unsigned before = releases;
    assert(create() == nullptr && releases == before + 1);
    expect_live(0);
    metadata_fail = false;
    allocation_error = -77;
    assert(create() == nullptr && original_calls == 1);
    allocation_error = 0;
    map_error = -88;
    assert(create() == nullptr && original_calls == 2);
    map_error = 0;
    original_fail = false;
    surface = create(640, 360);
    assert(surface && original_calls == 3 && !(surface->flags & SDL_PREALLOC));
    __wrap_SDL_FreeSurface_REAL(surface);
    std::array<SDL_Surface *, 4> held{};
    for (auto &entry : held)
    {
        entry = create();
        assert(entry && (entry->flags & SDL_PREALLOC));
    }
    expect_live(4);
    surface = create();
    assert(surface && !(surface->flags & SDL_PREALLOC));
    __wrap_SDL_FreeSurface_REAL(surface);
    for (auto *entry : held)
        __wrap_SDL_FreeSurface_REAL(entry);
    expect_live(0);
    // A cleanup error quarantines the old slot, rather than reusing its pixels.
    surface = create();
    void *retired = surface->pixels;
    unmap_error = -90;
    __wrap_SDL_FreeSurface_REAL(surface);
    expect_live(0);
    unmap_error = 0;
    surface = create();
    assert(surface && surface->pixels != retired);
    __wrap_SDL_FreeSurface_REAL(surface);
    __wrap_SDL_FreeSurface_REAL(nullptr);
    SDL_Quit();
}
