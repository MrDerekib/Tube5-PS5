// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_media_buffers.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
extern "C"
{
#include <libavutil/buffer.h>
    AVBufferRef *__wrap_av_buffer_alloc(std::size_t);
    AVBufferRef *__wrap_av_buffer_allocz(std::size_t);
    AVBufferRef *__real_av_buffer_create(std::uint8_t *, std::size_t,
                                         void (*)(void *, std::uint8_t *), void *, int);
}
namespace
{
constexpr std::size_t maximum = 2 * 1024 * 1024;
alignas(0x4000) std::uint8_t memory[32][maximum];
std::array<bool, 32> allocated{}, mapped{};
std::array<std::size_t, 32> lengths{};
std::mutex mutex;
std::atomic<unsigned> alloc_calls{0}, unmaps{0}, releases{0};
bool fail_allocate = false, fail_map = false, fail_create = false;
void expect_live(unsigned expected)
{
    char text[128]{};
    tube5::native_media_buffer_status(text, sizeof(text));
    unsigned live = 999, capacity = 0;
    assert(std::sscanf(text, "DBUF %u/%u", &live, &capacity) == 2);
    assert(live == expected && capacity == 32);
}
} // namespace
extern "C" std::size_t sceKernelGetDirectMemorySize()
{
    return sizeof(memory);
}
extern "C" int sceKernelAllocateDirectMemory(std::int64_t start, std::int64_t end,
                                             std::size_t bytes, std::size_t alignment, int type,
                                             std::int64_t *physical)
{
    assert(start == 0 && end == sizeof(memory) && bytes <= maximum && bytes % 0x4000 == 0);
    assert(alignment == 0x4000 && type == 12);
    ++alloc_calls;
    if (fail_allocate)
        return -77;
    std::lock_guard lock(mutex);
    for (unsigned i = 0; i < allocated.size(); ++i)
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
    const auto i = static_cast<unsigned>(physical / maximum);
    std::lock_guard lock(mutex);
    assert(allocated[i] && bytes == lengths[i] && protection == 0x33 && flags == 0);
    assert(alignment == 0x4000 && !mapped[i]);
    if (fail_map)
        return -88;
    *address = memory[i];
    mapped[i] = true;
    return 0;
}
extern "C" int sceKernelMunmap(void *address, std::size_t bytes)
{
    std::lock_guard lock(mutex);
    for (unsigned i = 0; i < allocated.size(); ++i)
        if (address == memory[i])
        {
            assert(allocated[i] && mapped[i] && bytes == lengths[i]);
            mapped[i] = false;
            ++unmaps;
            return 0;
        }
    assert(false);
    return -22;
}
extern "C" int sceKernelReleaseDirectMemory(std::int64_t physical, std::size_t bytes)
{
    const auto i = static_cast<unsigned>(physical / maximum);
    std::lock_guard lock(mutex);
    assert(allocated[i] && !mapped[i] && bytes == lengths[i]);
    allocated[i] = false;
    ++releases;
    return 0;
}
extern "C" AVBufferRef *__wrap_av_buffer_create(std::uint8_t *data, std::size_t bytes,
                                                void (*callback)(void *, std::uint8_t *),
                                                void *opaque, int flags)
{
    if (fail_create)
        return nullptr;
    return __real_av_buffer_create(data, bytes, callback, opaque, flags);
}
int main()
{
    AVBufferRef *buffer = __wrap_av_buffer_alloc(368768);
    assert(buffer && buffer->size == 368768);
    assert(reinterpret_cast<std::uintptr_t>(buffer->data) % 64 == 0);
    expect_live(1);
    assert(av_buffer_is_writable(buffer));
    std::memset(buffer->data, 0xa5, buffer->size);
    AVBufferRef *clone = av_buffer_ref(buffer);
    assert(clone && !av_buffer_is_writable(buffer));
    const unsigned before = unmaps;
    av_buffer_unref(&buffer);
    assert(unmaps == before && clone->data[368767] == 0xa5);
    av_buffer_unref(&clone);
    assert(unmaps == before + 1);
    expect_live(0);
    buffer = __wrap_av_buffer_allocz(368768);
    assert(buffer);
    for (std::size_t i = 0; i < buffer->size; ++i)
        assert(buffer->data[i] == 0);
    // A custom AVBuffer is copied on realloc, never passed to libc realloc.
    buffer->data[0] = 37;
    assert(av_buffer_realloc(&buffer, 500000) == 0 && buffer->data[0] == 37);
    expect_live(0);
    av_buffer_unref(&buffer);
    const unsigned native_before = alloc_calls;
    buffer = __wrap_av_buffer_alloc(16);
    assert(buffer && alloc_calls == native_before);
    av_buffer_unref(&buffer);
    buffer = __wrap_av_buffer_alloc(maximum + 1);
    assert(buffer && alloc_calls == native_before);
    av_buffer_unref(&buffer);
    fail_allocate = true;
    buffer = __wrap_av_buffer_alloc(368768);
    assert(buffer); // Native allocation failure delegates to the original allocator.
    av_buffer_unref(&buffer);
    fail_allocate = false;
    fail_map = true;
    buffer = __wrap_av_buffer_alloc(368768);
    assert(buffer);
    av_buffer_unref(&buffer);
    fail_map = false;
    fail_create = true;
    assert(__wrap_av_buffer_alloc(368768) == nullptr);
    expect_live(0);
    fail_create = false;
    std::array<AVBufferRef *, 33> buffers{};
    for (auto &item : buffers)
    {
        item = __wrap_av_buffer_alloc(368768);
        assert(item);
    }
    expect_live(32); // The 33rd buffer uses the original allocator.
    for (auto &item : buffers)
        av_buffer_unref(&item);
    expect_live(0);
    std::array<std::thread, 4> workers;
    for (auto &worker : workers)
        worker = std::thread(
            []
            {
                for (unsigned i = 0; i < 50; ++i)
                {
                    AVBufferRef *item = __wrap_av_buffer_allocz(368768);
                    assert(item && item->data[368767] == 0);
                    av_buffer_unref(&item);
                }
            });
    for (auto &worker : workers)
        worker.join();
    expect_live(0);
    assert(releases == unmaps + 1); // One mapping failure released without unmapping.
    for (bool item : allocated)
        assert(!item);
}
