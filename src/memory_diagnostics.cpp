// SPDX-License-Identifier: GPL-3.0-or-later
#include "memory_diagnostics.hpp"
#include <atomic>
#include <cstdio>
#include <limits>
namespace
{
static_assert(std::atomic<std::size_t>::is_always_lock_free);
std::atomic<unsigned> failures{0}, kind{0};
std::atomic<std::size_t> failed_bytes{0};
std::atomic<int> error{0};
} // namespace
namespace tube5
{
void reset_memory_diagnostic() noexcept
{
    failures = kind = 0;
    failed_bytes = 0;
    error = 0;
}
void memory_diagnostic_status(char *text, std::size_t capacity) noexcept
{
    constexpr const char *names[] = {"NONE", "MALLOC", "CALLOC", "REALLOC", "ALIGN"};
    const unsigned allocation = kind.load();
    std::snprintf(text, capacity, "MEMFAIL %u %s %zuB ERR %d", failures.load(),
                  allocation < 5 ? names[allocation] : "UNKNOWN", failed_bytes.load(),
                  error.load());
}
} // namespace tube5
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
namespace
{
void record(unsigned allocation, std::size_t bytes, int result) noexcept
{
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
