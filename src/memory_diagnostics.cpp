// SPDX-License-Identifier: GPL-3.0-or-later
#include "memory_diagnostics.hpp"
#include <atomic>
#include <cstdio>
#include <limits>
#include <cerrno>
namespace
{
static_assert(std::atomic<std::size_t>::is_always_lock_free);
std::atomic<unsigned> failures{0}, kind{0};
std::atomic<std::size_t> failed_bytes{0};
std::atomic<int> error{0};
std::atomic<bool> out_of_memory{false};
std::atomic<std::size_t> flexible_bytes_before_open{0};
std::atomic<int> flexible_query_result{-1};
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
extern "C" int sceKernelAvailableFlexibleMemorySize(std::size_t *size);
#endif
} // namespace
namespace tube5
{
bool memory_out_of_memory() noexcept
{
    return out_of_memory.load();
}
void reset_memory_diagnostic() noexcept
{
    out_of_memory = false;
    failures = kind = 0;
    failed_bytes = 0;
    error = 0;
    flexible_bytes_before_open = 0;
    flexible_query_result = -1;
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
    std::size_t available = 0;
    const int result = sceKernelAvailableFlexibleMemorySize(&available);
    flexible_query_result = result;
    if (result == 0)
        flexible_bytes_before_open = available;
#endif
}
void memory_diagnostic_status(char *text, std::size_t capacity) noexcept
{
    constexpr const char *names[] = {"NONE", "MALLOC", "CALLOC", "REALLOC", "ALIGN"};
    const unsigned allocation = kind.load();
    std::snprintf(text, capacity, "MEMFAIL %u %s %zuB ERR %d", failures.load(),
                  allocation < 5 ? names[allocation] : "UNKNOWN", failed_bytes.load(),
                  error.load());
}
void flexible_memory_status(char *text, std::size_t capacity) noexcept
{
    const int result = flexible_query_result.load();
    if (result == 0)
        std::snprintf(text, capacity, "FMEM BEFORE OPEN %zu MiB",
                      flexible_bytes_before_open.load() / (1024 * 1024));
    else if (result == -1)
        std::snprintf(text, capacity, "FMEM QUERY UNAVAILABLE");
    else
        std::snprintf(text, capacity, "FMEM QUERY ERR %d", result);
}
} // namespace tube5
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
namespace
{
void record(unsigned allocation, std::size_t bytes, int result) noexcept
{
    // Only the aligned API supplies a reliable error code in this diagnostic.
    // Do not confuse EINVAL (bad alignment) with allocator exhaustion.
    if (result == ENOMEM)
        out_of_memory = true;
    ++failures;
    kind = allocation;
    failed_bytes = bytes;
    error = result;
}
} // namespace
extern "C" void *__real_malloc(std::size_t);
extern "C" void *__real_calloc(std::size_t, std::size_t);
extern "C" void *__real_realloc(void *, std::size_t);
extern "C" int __real_posix_memalign(void **, std::size_t, std::size_t);
extern "C" void *__wrap_malloc(std::size_t bytes)
{
    void *result = __real_malloc(bytes);
    if (!result && bytes)
        record(1, bytes, 0);
    return result;
}
extern "C" void *__wrap_calloc(std::size_t count, std::size_t size)
{
    void *result = __real_calloc(count, size);
    if (!result && count && size)
        record(2,
               count > std::numeric_limits<std::size_t>::max() / size
                   ? std::numeric_limits<std::size_t>::max()
                   : count * size,
               0);
    return result;
}
extern "C" void *__wrap_realloc(void *address, std::size_t bytes)
{
    void *result = __real_realloc(address, bytes);
    if (!result && bytes)
        record(3, bytes, 0);
    return result;
}
extern "C" int __wrap_posix_memalign(void **address, std::size_t alignment, std::size_t bytes)
{
    const int result = __real_posix_memalign(address, alignment, bytes);
    if (result != 0 && bytes)
        record(4, bytes, result);
    return result;
}
#endif
