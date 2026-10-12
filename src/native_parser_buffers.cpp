// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_parser_buffers.hpp"
#ifdef TUBE5_PARSER_BUFFER_WRAP
#include "native_framebuffer.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifdef TUBE5_GPU_PLAYER
#define __real_free tube5_gpu_free
#define __real_realloc tube5_gpu_realloc
#endif
extern "C"
{
    void av_fast_malloc(void *, unsigned *, std::size_t);
    void ff_h2645_packet_split(); // Address only; never called through this declaration.
    void __real_free(void *);
    void *__real_realloc(void *, std::size_t);
    void *__real_av_fast_realloc(void *, unsigned *, std::size_t);
}
namespace
{
// Pinned PacBrew FFmpeg, checked against the linked ELF before signing.
// fast_malloc inlines av_malloc[z], so its aligned allocation bypasses the
// av_malloc wrapper. Preserve the original growth/zeroing/size-limit logic.
constexpr std::size_t minimum = 128 * 1024, maximum = 4 * 1024 * 1024;
constexpr unsigned slot_count = 8; // At most 32 MiB; no general-purpose malloc override.
struct Slot
{
    std::atomic<bool> claimed{false};
    std::atomic<void *> address{nullptr};
    tube5::NativeFramebuffer storage;
    std::size_t bytes = 0;
};
std::array<Slot, slot_count> slots{};
std::atomic<unsigned> live{0}, allocations{0}, failures{0};
std::atomic<std::size_t> live_bytes{0};
std::atomic<int> error{0};
bool release_owned(void *address) noexcept
{
    if (!address)
        return false;
    for (auto &slot : slots)
        if (slot.address.load(std::memory_order_acquire) == address)
        {
            const int result = slot.storage.release();
            if (result != 0)
            {
                ++failures;
                error = result;
                return true; // Quarantine: never send a native pointer to libc.
            }
            slot.address = nullptr;
            live_bytes -= slot.bytes;
            --live;
            slot.bytes = 0;
            slot.claimed.store(false, std::memory_order_release);
            return true;
        }
    return false;
}
} // namespace
namespace tube5
{
bool parser_aligned_caller(void *caller) noexcept
{
    const auto offset = reinterpret_cast<std::uintptr_t>(caller) -
                        reinterpret_cast<std::uintptr_t>(&av_fast_malloc);
    return offset == 0xfc || offset == 0x142;
}
bool parser_zero_caller(void *caller) noexcept
{
    return reinterpret_cast<std::uintptr_t>(caller) -
               reinterpret_cast<std::uintptr_t>(&ff_h2645_packet_split) ==
           0x126;
}
void *allocate_parser_buffer(std::size_t bytes, bool zero) noexcept
{
    if (bytes < minimum || bytes > maximum)
        return nullptr;
    for (auto &slot : slots)
    {
        bool available = false;
        if (!slot.claimed.compare_exchange_strong(available, true, std::memory_order_acquire))
            continue;
        const int result = slot.storage.allocate(bytes, 0x4000);
        if (result != 0)
        {
            ++failures;
            error = result;
            slot.claimed.store(false, std::memory_order_release);
            return nullptr; // Caller keeps the original allocator as fallback.
        }
        if (zero)
            std::memset(slot.storage.data(), 0, bytes);
        slot.bytes = (bytes + 0x3fff) & ~std::size_t{0x3fff};
        slot.address.store(slot.storage.data(), std::memory_order_release);
        live_bytes += slot.bytes;
        ++live;
        ++allocations;
        return slot.storage.data();
    }
    ++failures;
    error = -12;
    return nullptr;
}
void native_parser_buffer_status(char *text, std::size_t capacity) noexcept
{
    std::snprintf(text, capacity, "PBUF %u/8 %zu KIB ALLOC %u FAIL %u ERR %d", live.load(),
                  live_bytes.load() / 1024, allocations.load(), failures.load(), error.load());
}
bool reallocate_owned_parser_buffer(void *address, std::size_t bytes, void *&result) noexcept
{
    if (!address)
        return false;
    for (auto &slot : slots)
        if (slot.address.load(std::memory_order_acquire) == address)
        {
            if (!bytes)
            {
                (void)release_owned(address);
                result = nullptr;
                return true;
            }
            // Defensive migration if a caller uses realloc on a fast buffer.
            // Ordinary pointers never enter this path. Failure preserves the
            // old mapping, exactly as realloc requires.
            result = allocate_parser_buffer(bytes, false);
            if (!result)
                result = __real_realloc(nullptr, bytes);
            if (result)
            {
                std::memcpy(result, address, std::min(bytes, slot.bytes));
                (void)release_owned(address);
            }
            return true;
        }
    return false;
}
bool reallocate_mov_index(void *address, unsigned *size, std::size_t needed, void *caller,
                          void *&result) noexcept
{
    // mov_read_trun is a local symbol. Anchor its pinned return address to the
    // original public FFmpeg function; the build validates both callsites.
    const auto anchor = reinterpret_cast<std::uintptr_t>(&__real_av_fast_realloc);
    if (anchor - reinterpret_cast<std::uintptr_t>(caller) != 0xfcf03e || needed <= *size ||
        needed < minimum || needed > maximum || *size > maximum)
        return false;
    for (const auto &slot : slots)
        if (address && slot.address.load(std::memory_order_acquire) == address)
            return false; // Original function + owned realloc already handle it.
    void *staged = allocate_parser_buffer(std::max<std::size_t>(*size, minimum), false);
    if (!staged)
        return false;
    if (address && *size)
        std::memcpy(staged, address, *size);
    // Let FFmpeg enforce its own max_alloc_size/growth and update *size.
    // Its inline realloc recognizes the staged native pointer. Keep the libc
    // original untouched until success, including when FFmpeg refuses growth.
    result = __real_av_fast_realloc(staged, size, needed);
    if (result)
        __real_free(address);
    else
        (void)release_owned(staged);
    return true;
}
} // namespace tube5
extern "C" void __wrap_free(void *address)
{
    // FFmpeg's av_freep/fast_malloc inline free(), rather than av_free().
    // Only exact addresses owned by this bounded pool are intercepted.
    if (!release_owned(address))
        __real_free(address);
}
#endif
