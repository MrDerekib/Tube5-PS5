// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_framebuffer.hpp"
#include <cassert>
#include <limits>
namespace
{
int allocation_result = 0, mapping_result = 0;
int release_result = 0, unmap_result = 0;
std::size_t expected_bytes = 0x800000, expected_alignment = 0x200000;
unsigned allocations = 0, maps = 0, releases = 0, unmaps = 0;
std::size_t pool = 0x4000000;
char mapping;
} // namespace
extern "C" std::size_t sceKernelGetDirectMemorySize()
{
    return pool;
}
extern "C" int sceKernelAllocateDirectMemory(std::int64_t start, std::int64_t end,
                                             std::size_t bytes, std::size_t alignment, int type,
                                             std::int64_t *physical)
{
    assert(start == 0 && end == static_cast<std::int64_t>(pool));
    assert(bytes == expected_bytes && alignment == expected_alignment && type == 12);
    ++allocations;
    *physical = 0; // Physical address zero is valid and must still be released.
    return allocation_result;
}
extern "C" int sceKernelMapDirectMemory(void **address, std::size_t bytes, int protection,
                                        int flags, std::int64_t physical, std::size_t alignment)
{
    assert(bytes == expected_bytes && protection == 0x33 && flags == 0 && physical == 0 &&
           alignment == expected_alignment);
    ++maps;
    if (mapping_result == 0)
        *address = &mapping;
    return mapping_result;
}
extern "C" int sceKernelReleaseDirectMemory(std::int64_t physical, std::size_t bytes)
{
    assert(physical == 0 && bytes == expected_bytes);
    ++releases;
    return release_result;
}
extern "C" int sceKernelMunmap(void *address, std::size_t bytes)
{
    assert(address == &mapping && bytes == expected_bytes);
    ++unmaps;
    return unmap_result;
}
int main()
{
    {
        tube5::NativeFramebuffer framebuffer;
        assert(framebuffer.allocate(0) == -22);
        assert(framebuffer.allocate(std::numeric_limits<std::size_t>::max()) == -22);
        pool = 0x400000;
        assert(framebuffer.allocate(1920 * 1080 * 4) == -12 && allocations == 0);
        pool = 0x4000000;
        allocation_result = -77;
        assert(framebuffer.allocate(1920 * 1080 * 4) == -77 && maps == 0);
        allocation_result = 0;
        mapping_result = -88;
        assert(framebuffer.allocate(1920 * 1080 * 4) == -88 && releases == 1);
        mapping_result = 0;
        assert(framebuffer.allocate(1920 * 1080 * 4) == 0 && framebuffer.data() == &mapping);
        assert(framebuffer.allocate(1920 * 1080 * 4) == -22);
    }
    assert(allocations == 3 && maps == 2 && unmaps == 1 && releases == 2);
    expected_bytes = 0x5c000;
    expected_alignment = 0x4000;
    tube5::NativeFramebuffer buffer;
    assert(buffer.allocate(368768, 0) == -22);
    assert(buffer.allocate(368768, 0x2000) == -22);
    assert(buffer.allocate(368768, 0x6000) == -22);
    assert(buffer.allocate(368768, 0x4000) == 0);
    unmap_result = -90;
    assert(buffer.release() == -90 && buffer.data() == &mapping);
    unmap_result = 0;
    release_result = -91;
    assert(buffer.release() == -91 && buffer.data() == nullptr);
    assert(buffer.allocate(368768, 0x4000) == -22);
    release_result = 0;
    assert(buffer.release() == 0);
    const unsigned released = releases;
    assert(buffer.release() == 0 && releases == released);
    assert(buffer.allocate(368768, 0x4000) == 0);
}
