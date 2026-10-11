// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_refstruct_buffers.hpp"
#ifdef TUBE5_REFSTRUCT_BUFFER_WRAP
#include "native_framebuffer.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
extern "C"
{
    void *ff_refstruct_pool_get(void *);
    void *__real_av_malloc(std::size_t);
    void __real_av_free(void *);
}
namespace
{
// Private ABI of pinned PacBrew FFmpeg 61.3.100. Build validation refuses a
// different callsite/free path. No TLS, stack walking or global libc allocator.
constexpr std::uintptr_t refstruct_return_offset = 0x99;
constexpr unsigned slot_count = 32;
// Some valid 360p H.264 streams need ~59 KiB RefStruct buffers. Keeping
// these on the constrained libc heap can fail before the first output frame.
// Still restricted to the pinned pool callsite, with the same slots/cap.
constexpr std::size_t minimum = 32 * 1024, maximum = 8 * 1024 * 1024;
struct Slot
{
    std::atomic<bool> claimed{false};
    std::atomic<void *> address{nullptr};
    tube5::NativeFramebuffer memory;
    std::size_t bytes = 0;
};
Slot slots[slot_count];
std::atomic<unsigned> live{0}, allocations{0}, failures{0};
std::atomic<std::size_t> live_bytes{0};
std::atomic<int> error{0};
} // namespace
namespace tube5
{
void *allocate_refstruct_buffer(std::size_t bytes, void *caller) noexcept
{
    const auto expected =
        reinterpret_cast<std::uintptr_t>(&ff_refstruct_pool_get) + refstruct_return_offset;
    if (reinterpret_cast<std::uintptr_t>(caller) == expected && bytes >= minimum &&
        bytes <= maximum)
        for (auto &slot : slots)
        {
            bool available = false;
            if (!slot.claimed.compare_exchange_strong(available, true))
                continue;
            const int result = slot.memory.allocate(bytes, 0x4000);
            if (result != 0)
            {
                ++failures;
                error = result;
                slot.claimed = false;
                break;
            }
            slot.bytes = (bytes + 0x3fff) & ~std::size_t{0x3fff};
            slot.address = slot.memory.data();
            live_bytes += slot.bytes;
            ++live;
            ++allocations;
            return slot.memory.data();
        }
    return __real_av_malloc(bytes);
}
void native_refstruct_buffer_status(char *text, std::size_t capacity) noexcept
{
    std::snprintf(text, capacity, "RBUF %u/32 %zu KIB ALLOC %u FAIL %u ERR %d", live.load(),
                  live_bytes.load() / 1024, allocations.load(), failures.load(), error.load());
}
} // namespace tube5
extern "C" void __wrap_av_free(void *address)
{
    if (address)
        for (auto &slot : slots)
            if (slot.address.load() == address)
            {
                slot.address = nullptr;
                const int result = slot.memory.release();
                if (result == 0)
                {
                    live_bytes -= slot.bytes;
                    --live;
                    slot.bytes = 0;
                    slot.claimed = false;
                }
                else
                {
                    ++failures;
                    error = result; // Quarantined; never forward to libc.
                }
                return;
            }
    __real_av_free(address);
}
#endif
