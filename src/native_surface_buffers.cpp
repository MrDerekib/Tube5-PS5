// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_surface_buffers.hpp"
#include "native_framebuffer.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>

#ifdef TUBE5_SURFACE_BUFFER_WRAP
namespace
{
constexpr unsigned slot_count = 4; // At most 32 MiB, separate from FFmpeg's pool.
struct Slot
{
    std::atomic<bool> claimed{false};
    std::atomic<SDL_Surface *> surface{nullptr};
    tube5::NativeFramebuffer pixels;
};
Slot slots[slot_count];
struct YuvSlot
{
    std::atomic<bool> claimed{false};
    std::atomic<void *> address{nullptr};
    tube5::NativeFramebuffer pixels;
};
YuvSlot yuv_slots[2]; // Only SDL's fixed-size YUV texture planes; at most 8 MiB.
std::atomic<unsigned> yuv_live{0}, yuv_allocations{0}, yuv_failures{0};
std::atomic<int> yuv_error{0};
std::atomic<unsigned> live{0}, allocations{0}, failures{0};
std::atomic<int> last_error{0};
void release_slot(Slot &slot) noexcept
{
    const int result = slot.pixels.release();
    if (result == 0)
        slot.claimed = false;
    else
    {
        ++failures;
        last_error = result;
        // Quarantine storage whose unmap/release failed; never reuse it.
    }
}
} // namespace

extern "C" SDL_Surface *__real_SDL_CreateRGBSurface_REAL(Uint32, int, int, int, Uint32, Uint32,
                                                         Uint32, Uint32);
extern "C" void __real_SDL_FreeSurface_REAL(SDL_Surface *);
extern "C" void *SDL_SW_CreateYUVTexture(Uint32, int, int);
extern "C" void *__real_SDL_SIMDAlloc_REAL(std::size_t);
extern "C" void __real_SDL_SIMDFree_REAL(void *);
namespace tube5
{
void *allocate_texture_yuv(std::size_t bytes, void *caller) noexcept
{
    // Pinned SDL2: the sole SIMD allocation in SW_CreateYUVTexture returns
    // at +0xc8. The native build checks both this callsite and its cleanup.
    const auto origin = reinterpret_cast<std::uintptr_t>(caller);
    const auto anchor = reinterpret_cast<std::uintptr_t>(&SDL_SW_CreateYUVTexture);
    if (origin >= anchor && origin - anchor == 0xc8 && bytes >= 64 * 1024 &&
        bytes <= 4 * 1024 * 1024)
        for (auto &slot : yuv_slots)
        {
            bool available = false;
            if (!slot.claimed.compare_exchange_strong(available, true))
                continue;
            const int result = slot.pixels.allocate(bytes, 0x4000);
            if (result != 0)
            {
                ++yuv_failures;
                yuv_error = result;
                const int cleanup = slot.pixels.release();
                if (cleanup == 0)
                    slot.claimed = false;
                else
                    yuv_error = cleanup; // Quarantine failed cleanup.
                break;
            }
            slot.address = slot.pixels.data();
            ++yuv_live;
            ++yuv_allocations;
            return slot.pixels.data();
        }
    return __real_SDL_SIMDAlloc_REAL(bytes);
}
SDL_Surface *create_texture_surface(Uint32 flags, int width, int height, int depth, Uint32 red,
                                    Uint32 green, Uint32 blue, Uint32 alpha) noexcept
{
    if (flags == 0 && depth == 32 && width > 0 && width <= 1920 && height > 0 && height <= 1080)
    {
        const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
        if (bytes >= 1024 * 1024)
            for (auto &slot : slots)
            {
                bool available = false;
                if (!slot.claimed.compare_exchange_strong(available, true))
                    continue;
                const int result = slot.pixels.allocate(bytes, 0x4000);
                if (result != 0)
                {
                    ++failures;
                    last_error = result;
                    release_slot(slot);
                    break; // Preserve the original allocator as fallback.
                }
                std::memset(slot.pixels.data(), 0, bytes);
                SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(
                    slot.pixels.data(), width, height, depth, width * 4, red, green, blue, alpha);
                if (!surface)
                {
                    release_slot(slot);
                    return nullptr;
                }
                // SDL_PREALLOC means SDL owns metadata, not our pixel storage.
                slot.surface = surface;
                ++live;
                ++allocations;
                return surface;
            }
    }
    return __real_SDL_CreateRGBSurface_REAL(flags, width, height, depth, red, green, blue, alpha);
}
} // namespace tube5
extern "C" void *__wrap_SDL_SIMDAlloc_REAL(std::size_t bytes)
{
    return tube5::allocate_texture_yuv(bytes, __builtin_return_address(0));
}
extern "C" void __wrap_SDL_SIMDFree_REAL(void *address)
{
    if (address)
        for (auto &slot : yuv_slots)
            if (slot.address.load() == address)
            {
                slot.address = nullptr;
                --yuv_live;
                const int result = slot.pixels.release();
                if (result == 0)
                    slot.claimed = false;
                else
                {
                    ++yuv_failures;
                    yuv_error = result;
                }
                return;
            }
    __real_SDL_SIMDFree_REAL(address);
}
extern "C" void __wrap_SDL_FreeSurface_REAL(SDL_Surface *surface)
{
    if (surface)
        for (auto &slot : slots)
            if (slot.surface.load() == surface)
            {
                // Match SDL2's final-reference semantics, including extra refs.
                if (surface->refcount > 1)
                {
                    __real_SDL_FreeSurface_REAL(surface);
                    return;
                }
                slot.surface = nullptr;
                __real_SDL_FreeSurface_REAL(surface);
                --live;
                release_slot(slot);
                return;
            }
    __real_SDL_FreeSurface_REAL(surface);
}
namespace tube5
{
void native_surface_buffer_status(char *text, std::size_t capacity) noexcept
{
    std::snprintf(text, capacity,
                  "SBUF %u/4 ALLOC %u FAIL %u ERR %d YBUF %u/2 ALLOC %u FAIL %u ERR %d",
                  live.load(), allocations.load(), failures.load(), last_error.load(),
                  yuv_live.load(), yuv_allocations.load(), yuv_failures.load(), yuv_error.load());
}
} // namespace tube5
#endif
