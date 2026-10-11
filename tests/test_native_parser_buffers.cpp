// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_parser_buffers.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C"
{
    void __wrap_free(void *);
    void av_fast_malloc(void *, unsigned *, std::size_t)
    {
    }
    void ff_h2645_packet_split()
    {
    }
}
namespace
{
struct Mapping
{
    void *address = nullptr;
    std::size_t bytes = 0;
    bool allocated = false, mapped = false;
};
std::array<Mapping, 8> maps{};
unsigned original_frees = 0;
int allocation_error = 0;
int unmap_error = 0, release_error = 0;
bool realloc_error = false;
bool fast_limit_error = false;
unsigned fast_calls = 0;
void live(unsigned expected)
{
    char status[128]{}, prefix[32]{};
    tube5::native_parser_buffer_status(status, sizeof(status));
    std::snprintf(prefix, sizeof(prefix), "PBUF %u/8 ", expected);
    assert(std::strncmp(status, prefix, std::strlen(prefix)) == 0);
}
} // namespace
extern "C" std::size_t sceKernelGetDirectMemorySize()
{
    return 32 * 1024 * 1024;
}
extern "C" int sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t bytes,
                                             std::size_t alignment, int type,
                                             std::int64_t *physical)
{
    assert(bytes <= 4 * 1024 * 1024 && bytes % 0x4000 == 0 && alignment == 0x4000 && type == 12);
    if (allocation_error)
        return allocation_error;
    for (unsigned i = 0; i < maps.size(); ++i)
        if (!maps[i].allocated)
        {
            maps[i].allocated = true;
            maps[i].bytes = bytes;
            *physical = i;
            return 0;
        }
    return -12;
}
extern "C" int sceKernelMapDirectMemory(void **address, std::size_t bytes, int protection,
                                        int flags, std::int64_t physical, std::size_t alignment)
{
    auto &map = maps.at(physical);
    assert(map.allocated && !map.mapped && bytes == map.bytes && protection == 0x33 && flags == 0);
    if (posix_memalign(&map.address, alignment, bytes))
        return -12;
    std::memset(map.address, 0xa5, bytes);
    map.mapped = true;
    *address = map.address;
    return 0;
}
extern "C" int sceKernelMunmap(void *address, std::size_t bytes)
{
    for (auto &map : maps)
        if (map.address == address)
        {
            assert(map.mapped && bytes == map.bytes);
            if (unmap_error)
                return unmap_error;
            map.mapped = false;
            return 0;
        }
    assert(false);
    return -22;
}
extern "C" int sceKernelReleaseDirectMemory(std::int64_t physical, std::size_t bytes)
{
    auto &map = maps.at(physical);
    assert(map.allocated && !map.mapped && bytes == map.bytes);
    if (release_error)
        return release_error;
    std::free(map.address);
    map = {};
    return 0;
}
extern "C" void __real_free(void *address)
{
    for (const auto &map : maps)
        assert(!address || map.address != address);
    ++original_frees;
    std::free(address);
}
extern "C" void *__real_realloc(void *address, std::size_t bytes)
{
    assert(!address);
    return realloc_error ? nullptr : std::realloc(address, bytes);
}
extern "C" void *__real_av_fast_realloc(void *address, unsigned *size, std::size_t needed)
{
    ++fast_calls;
    if (needed <= *size)
        return address;
    const auto grown = needed + needed / 16 + 32;
    void *result = nullptr;
    if (!fast_limit_error)
    {
        if (!tube5::reallocate_owned_parser_buffer(address, grown, result))
            result = std::realloc(address, grown);
    }
    *size = result ? static_cast<unsigned>(grown) : 0;
    return result;
}
int main()
{
    using namespace tube5;
    const auto fast = reinterpret_cast<std::uintptr_t>(&av_fast_malloc);
    const auto split = reinterpret_cast<std::uintptr_t>(&ff_h2645_packet_split);
    assert(parser_aligned_caller(reinterpret_cast<void *>(fast + 0xfc)));
    assert(parser_aligned_caller(reinterpret_cast<void *>(fast + 0x142)));
    assert(!parser_aligned_caller(reinterpret_cast<void *>(fast + 0xfd)));
    assert(parser_zero_caller(reinterpret_cast<void *>(split + 0x126)));
    assert(!parser_zero_caller(reinterpret_cast<void *>(split + 0x125)));
    assert(!allocate_parser_buffer(0, true));
    assert(!allocate_parser_buffer(128 * 1024 - 1, true));
    assert(!allocate_parser_buffer(4 * 1024 * 1024 + 1, true));
    live(0);
    auto *zero = static_cast<unsigned char *>(allocate_parser_buffer(578452, true));
    assert(zero && reinterpret_cast<std::uintptr_t>(zero) % 64 == 0);
    for (unsigned i = 0; i < 578452; ++i)
        assert(zero[i] == 0);
    live(1);
    __wrap_free(zero);
    live(0);
    auto *raw = static_cast<unsigned char *>(allocate_parser_buffer(299924, false));
    assert(raw && raw[0] == 0xa5);
    __wrap_free(raw);
    assert(original_frees == 0);
    raw = static_cast<unsigned char *>(allocate_parser_buffer(299924, false));
    std::memset(raw, 0x42, 299924);
    void *grown = nullptr;
    assert(reallocate_owned_parser_buffer(raw, 578452, grown) && grown);
    live(1);
    for (unsigned i = 0; i < 299924; ++i)
        assert(static_cast<unsigned char *>(grown)[i] == 0x42);
    allocation_error = -12;
    realloc_error = true;
    void *failed = nullptr;
    assert(reallocate_owned_parser_buffer(grown, 700000, failed) && !failed);
    live(1);
    assert(static_cast<unsigned char *>(grown)[0] == 0x42);
    allocation_error = 0;
    realloc_error = false;
    void *migrated = nullptr;
    assert(reallocate_owned_parser_buffer(grown, 4 * 1024 * 1024 + 1, migrated) && migrated);
    live(0);
    assert(static_cast<unsigned char *>(migrated)[0] == 0x42);
    __wrap_free(migrated);
    raw = static_cast<unsigned char *>(allocate_parser_buffer(299924, true));
    assert(reallocate_owned_parser_buffer(raw, 0, grown) && !grown);
    live(0);
    assert(!reallocate_owned_parser_buffer(nullptr, 128, grown));
    __wrap_free(std::malloc(32));
    __wrap_free(nullptr);
    assert(original_frees == 3);
    for (int *failure : {&unmap_error, &release_error})
    {
        raw = static_cast<unsigned char *>(allocate_parser_buffer(299924, true));
        assert(raw);
        *failure = -5;
        __wrap_free(raw);
        live(1); // Quarantined; must never reach the original libc free.
        assert(original_frees == 3);
        auto *other = allocate_parser_buffer(299924, true);
        assert(other && other != raw);
        *failure = 0;
        __wrap_free(other);
        __wrap_free(raw); // Test-only retry once the mocked kernel recovers.
        live(0);
    }
    std::array<void *, 8> occupied{};
    for (auto &address : occupied)
    {
        address = allocate_parser_buffer(128 * 1024, true);
        assert(address);
    }
    live(8);
    assert(!allocate_parser_buffer(128 * 1024, true));
    for (auto *address : occupied)
        __wrap_free(address);
    live(0);
    allocation_error = -12;
    assert(!allocate_parser_buffer(128 * 1024, true));
    live(0);
    allocation_error = 0;
    // Bootstrap only the pinned MOV index caller. Preserve libc contents,
    // let the original growth run, and never free the original on failure.
    const auto mov_caller = reinterpret_cast<void *>(
        reinterpret_cast<std::uintptr_t>(&__real_av_fast_realloc) - 0xfcf03e);
    auto *original = static_cast<unsigned char *>(std::malloc(185978));
    assert(original);
    std::memset(original, 0x71, 185978);
    unsigned original_size = 185978;
    void *index = nullptr;
    assert(!reallocate_mov_index(original, &original_size, 187632, nullptr, index));
    fast_limit_error = true;
    assert(reallocate_mov_index(original, &original_size, 187632, mov_caller, index));
    assert(!index && original_size == 0 && original[185977] == 0x71);
    live(0);
    fast_limit_error = false;
    original_size = 185978;
    assert(reallocate_mov_index(original, &original_size, 187632, mov_caller, index) && index);
    assert(original_size == 199391 && fast_calls == 2);
    for (unsigned i = 0; i < 185978; ++i)
        assert(static_cast<unsigned char *>(index)[i] == 0x71);
    live(1);
    assert(!reallocate_mov_index(index, &original_size, 250000, mov_caller, grown));
    grown = __real_av_fast_realloc(index, &original_size, 250000);
    assert(grown && static_cast<unsigned char *>(grown)[185977] == 0x71);
    __wrap_free(grown);
    live(0);
    unsigned empty_size = 0;
    assert(reallocate_mov_index(nullptr, &empty_size, 150000, mov_caller, index) && index);
    __wrap_free(index);
    allocation_error = -12;
    empty_size = 0;
    assert(!reallocate_mov_index(nullptr, &empty_size, 150000, mov_caller, index));
    allocation_error = 0;
    assert(!reallocate_mov_index(nullptr, &empty_size, 1000, mov_caller, index));
    assert(!reallocate_mov_index(nullptr, &empty_size, 4 * 1024 * 1024 + 1, mov_caller, index));
    live(0);
    for (unsigned i = 0; i < 100; ++i)
    {
        auto *address = allocate_parser_buffer(299924 + i, true);
        assert(address);
        __wrap_free(address);
    }
    live(0);
}
