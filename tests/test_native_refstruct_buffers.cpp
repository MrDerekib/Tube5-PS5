// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_refstruct_buffers.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C"
{
#include <libavutil/mem.h>
    void __wrap_av_free(void *);
    void *ff_refstruct_pool_get(void *)
    {
        return nullptr;
    }
}
namespace
{
struct Allocation
{
    bool allocated = false, mapped = false;
    void *address = nullptr;
    std::size_t bytes = 0;
};
std::array<Allocation, 32> storage{};
unsigned originals = 0, original_frees = 0;
int allocation_error = 0, unmap_error = 0;
constexpr std::size_t maximum = 8 * 1024 * 1024;
void *caller(unsigned offset = 0x99)
{
    return reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(&ff_refstruct_pool_get) +
                                    offset);
}
void expect_live(unsigned count)
{
    char text[128]{}, prefix[32]{};
    tube5::native_refstruct_buffer_status(text, sizeof(text));
    std::snprintf(prefix, sizeof(prefix), "RBUF %u/32 ", count);
    assert(std::strncmp(text, prefix, std::strlen(prefix)) == 0);
}
} // namespace
extern "C" std::size_t sceKernelGetDirectMemorySize()
{
    return 32 * maximum;
}
extern "C" int sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t bytes,
                                             std::size_t alignment, int type,
                                             std::int64_t *physical)
{
    assert(bytes <= maximum && bytes % 0x4000 == 0 && alignment == 0x4000 && type == 12);
    if (allocation_error)
        return allocation_error;
    for (unsigned i = 0; i < storage.size(); ++i)
        if (!storage[i].allocated)
        {
            storage[i].allocated = true;
            storage[i].bytes = bytes;
            *physical = i;
            return 0;
        }
    return -12;
}
extern "C" int sceKernelMapDirectMemory(void **address, std::size_t bytes, int protection,
                                        int flags, std::int64_t physical, std::size_t alignment)
{
    auto &slot = storage.at(physical);
    assert(slot.allocated && !slot.mapped && bytes == slot.bytes && protection == 0x33 &&
           flags == 0);
    if (posix_memalign(&slot.address, alignment, bytes) != 0)
        return -12;
    slot.mapped = true;
    *address = slot.address;
    return 0;
}
extern "C" int sceKernelMunmap(void *address, std::size_t bytes)
{
    for (auto &slot : storage)
        if (slot.address == address)
        {
            assert(slot.mapped && bytes == slot.bytes);
            if (unmap_error)
                return unmap_error;
            slot.mapped = false;
            return 0;
        }
    assert(false);
    return -22;
}
extern "C" int sceKernelReleaseDirectMemory(std::int64_t physical, std::size_t bytes)
{
    auto &slot = storage.at(physical);
    assert(slot.allocated && !slot.mapped && slot.bytes == bytes);
    std::free(slot.address);
    slot = {};
    return 0;
}
extern "C" void *__real_av_malloc(std::size_t bytes)
{
    ++originals;
    return av_malloc(bytes);
}
extern "C" void __real_av_free(void *address)
{
    for (const auto &slot : storage)
        assert(!address || slot.address != address);
    ++original_frees;
    av_free(address);
}
int main()
{
    void *buffer = tube5::allocate_refstruct_buffer(5234088, caller());
    assert(buffer && reinterpret_cast<std::uintptr_t>(buffer) % 64 == 0 && originals == 0);
    expect_live(1);
    std::memset(buffer, 0xa5, 5234088);
    __wrap_av_free(buffer);
    expect_live(0);
    assert(original_frees == 0);
    buffer = tube5::allocate_refstruct_buffer(5234088, caller(0x98));
    assert(buffer && originals == 1);
    __wrap_av_free(buffer);
    assert(original_frees == 1);
    allocation_error = -77;
    buffer = tube5::allocate_refstruct_buffer(5234088, caller());
    assert(buffer && originals == 2);
    __wrap_av_free(buffer);
    allocation_error = 0;
    buffer = tube5::allocate_refstruct_buffer(64, caller());
    assert(buffer && originals == 3);
    __wrap_av_free(buffer);
    buffer = tube5::allocate_refstruct_buffer(maximum + 1, caller());
    assert(buffer && originals == 4);
    __wrap_av_free(buffer);
    std::array<void *, 32> held{};
    for (auto &entry : held)
    {
        entry = tube5::allocate_refstruct_buffer(5234088, caller());
        assert(entry);
    }
    expect_live(32);
    buffer = tube5::allocate_refstruct_buffer(5234088, caller());
    assert(buffer && originals == 5);
    __wrap_av_free(buffer);
    for (auto *entry : held)
        __wrap_av_free(entry);
    expect_live(0);
    buffer = tube5::allocate_refstruct_buffer(5234088, caller());
    unmap_error = -90;
    __wrap_av_free(buffer);
    expect_live(1);
    unmap_error = 0;
    void *replacement = tube5::allocate_refstruct_buffer(5234088, caller());
    assert(replacement && replacement != buffer);
    __wrap_av_free(replacement);
    __wrap_av_free(nullptr);
    // The quarantined mapping is retried by NativeFramebuffer's destructor.
}
