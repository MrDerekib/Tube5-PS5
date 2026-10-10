// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_media_buffers.hpp"
#ifdef TUBE5_MEDIA_BUFFER_WRAP
#include "native_framebuffer.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
extern "C"
{
#include <libavutil/buffer.h>
    AVBufferRef *__real_av_buffer_alloc(std::size_t);
    AVBufferRef *__real_av_buffer_allocz(std::size_t);
}
namespace
{
constexpr std::size_t page = 0x4000;
constexpr std::size_t minimum = 128 * 1024, maximum = 2 * 1024 * 1024;
constexpr unsigned slot_count = 32; // At most 64 MiB, even with every slot occupied.
struct Slot
{
    std::atomic<bool> claimed{false};
    tube5::NativeFramebuffer storage;
    std::size_t bytes = 0;
};
std::array<Slot, slot_count> slots{};
std::atomic<unsigned> live{0}, allocations{0}, failures{0};
std::atomic<std::size_t> live_bytes{0};
std::atomic<int> last_error{0};
void release_buffer(void *opaque, std::uint8_t *) noexcept
{
    auto &slot = *static_cast<Slot *>(opaque);
    const int result = slot.storage.release();
    if (result != 0)
    {
        // Quarantine the slot if native cleanup failed; never reuse its mapping.
        last_error = result;
        ++failures;
        return;
    }
    live_bytes -= slot.bytes;
    --live;
    slot.bytes = 0;
    slot.claimed.store(false, std::memory_order_release);
}
AVBufferRef *allocate_buffer(std::size_t bytes, bool zero) noexcept
{
    auto original = zero ? __real_av_buffer_allocz : __real_av_buffer_alloc;
    if (bytes < minimum || bytes > maximum)
        return original(bytes);
    for (auto &slot : slots)
    {
        bool expected = false;
        if (!slot.claimed.compare_exchange_strong(expected, true, std::memory_order_acquire))
            continue;
        const int result = slot.storage.allocate(bytes, page);
        if (result != 0)
        {
            last_error = result;
            ++failures;
            slot.claimed.store(false, std::memory_order_release);
            return original(bytes);
        }
        slot.bytes = (bytes + page - 1) & ~(page - 1);
        live_bytes += slot.bytes;
        ++live;
        auto *data = static_cast<std::uint8_t *>(slot.storage.data());
        if (zero)
            std::memset(data, 0, bytes);
        AVBufferRef *buffer = av_buffer_create(data, bytes, release_buffer, &slot, 0);
        if (!buffer)
        {
            // av_buffer_create leaves caller-owned data untouched on failure.
            release_buffer(&slot, data);
            return nullptr;
        }
        ++allocations;
        return buffer;
    }
    last_error = -12;
    ++failures;
    return original(bytes);
}
} // namespace
namespace tube5
{
void native_media_buffer_status(char *text, std::size_t capacity) noexcept
{
    std::snprintf(text, capacity, "DBUF %u/%u %zu KIB ALLOC %u FAIL %u ERR %d", live.load(),
                  slot_count, live_bytes.load() / 1024, allocations.load(), failures.load(),
                  last_error.load());
}
} // namespace tube5
extern "C" AVBufferRef *__wrap_av_buffer_alloc(std::size_t bytes)
{
    return allocate_buffer(bytes, false);
}
extern "C" AVBufferRef *__wrap_av_buffer_allocz(std::size_t bytes)
{
    return allocate_buffer(bytes, true);
}
#endif
