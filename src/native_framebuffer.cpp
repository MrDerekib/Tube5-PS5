// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_framebuffer.hpp"
#include <limits>
extern "C"
{
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t, int,
                                      std::int64_t *);
    int sceKernelMapDirectMemory(void **, std::size_t, int, int, std::int64_t, std::size_t);
    int sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
    int sceKernelMunmap(void *, std::size_t);
}
namespace tube5
{
NativeFramebuffer::~NativeFramebuffer()
{
    (void)release();
}
int NativeFramebuffer::release() noexcept
{
    if (mapped_)
    {
        const int result = sceKernelMunmap(mapped_, bytes_);
        if (result != 0)
            return result;
        mapped_ = nullptr;
    }
    if (bytes_)
    {
        const int result = sceKernelReleaseDirectMemory(physical_, bytes_);
        if (result != 0)
            return result;
        bytes_ = 0;
        physical_ = 0;
    }
    return 0;
}
int NativeFramebuffer::allocate(std::size_t bytes, std::size_t alignment) noexcept
{
    if (bytes_ || !bytes || alignment < 0x4000 || (alignment & (alignment - 1)) != 0 ||
        bytes > std::numeric_limits<std::size_t>::max() - (alignment - 1))
        return -22;
    const std::size_t rounded = (bytes + alignment - 1) & ~(alignment - 1);
    const std::size_t pool = sceKernelGetDirectMemorySize();
    if (rounded > pool || pool > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        return -12;
    std::int64_t physical = 0;
    // This is CPU working storage, not a scanout buffer. SDL reads it while
    // composing and the tiler reads every pixel. Type 3 is write-combined and
    // makes those reads expensive; the PS5 Vulkan project's coherence probe
    // confirms that type 12 with protection 0x33 is CPU cached.
    constexpr int cpu_cached_memory = 12;
    int result = sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool), rounded,
                                               alignment, cpu_cached_memory, &physical);
    if (result != 0)
        return result;
    void *mapped = nullptr;
    result = sceKernelMapDirectMemory(&mapped, rounded, 0x33, 0, physical, alignment);
    if (result != 0 || !mapped)
    {
        (void)sceKernelReleaseDirectMemory(physical, rounded);
        return result != 0 ? result : -12;
    }
    physical_ = physical;
    bytes_ = rounded;
    mapped_ = mapped;
    return 0;
}
} // namespace tube5
