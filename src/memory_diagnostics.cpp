// SPDX-License-Identifier: GPL-3.0-or-later
#include "memory_diagnostics.hpp"
#include "native_refstruct_buffers.hpp"
#include "native_parser_buffers.hpp"
#include <atomic>
#include <cstdio>
#include <limits>
#include <cerrno>
#include <cstdint>
namespace
{
static_assert(std::atomic<std::size_t>::is_always_lock_free);
std::atomic<unsigned> failures{0}, kind{0};
std::atomic<std::size_t> failed_bytes{0};
std::atomic<int> error{0};
std::atomic<bool> out_of_memory{false};
std::atomic<const char *> current_stage{"IDLE"}, failure_stage{"NONE"};
std::atomic<std::size_t> flexible_bytes_before_open{0};
std::atomic<int> flexible_query_result{-1};
std::atomic<bool> playback_audio_output{false}, playback_video_output{false};
struct AllocationOrigin
{
    unsigned kind = 0;
    std::size_t bytes = 0, alignment = 0;
    std::intptr_t relative = 0;
};
// Publish immutable snapshots; simultaneous worker failures cannot mix fields.
std::atomic<unsigned> first_origin_state{0}, ffmpeg_origin_state{0};
AllocationOrigin first_origin{}, ffmpeg_origin{};
#if defined(TUBE5_DIAGNOSTICS) && defined(TUBE5_PS5_NATIVE)
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
    current_stage = "SOURCE_OPEN";
    failure_stage = "NONE";
    flexible_bytes_before_open = 0;
    flexible_query_result = -1;
    playback_audio_output = false;
    playback_video_output = false;
    first_origin_state = 0;
    ffmpeg_origin_state = 0;
#if defined(TUBE5_DIAGNOSTICS) && defined(TUBE5_PS5_NATIVE)
    std::size_t available = 0;
    const int result = sceKernelAvailableFlexibleMemorySize(&available);
    flexible_query_result = result;
    if (result == 0)
        flexible_bytes_before_open = available;
#endif
}
void memory_set_stage(const char *stage) noexcept
{
    current_stage = stage ? stage : "UNKNOWN";
}
void memory_mark_playback_output(bool video) noexcept
{
    (video ? playback_video_output : playback_audio_output) = true;
}
void memory_diagnostic_status(char *text, std::size_t capacity) noexcept
{
    constexpr const char *names[] = {"NONE", "MALLOC", "CALLOC", "REALLOC", "ALIGN"};
    const unsigned allocation = kind.load();
    std::snprintf(text, capacity, "MEMFAIL %u %s %zuB ERR %d", failures.load(),
                  allocation < 5 ? names[allocation] : "UNKNOWN", failed_bytes.load(),
                  error.load());
}
void memory_failure_stage_status(char *text, std::size_t capacity) noexcept
{
    std::snprintf(text, capacity, "ALLOC FAIL STAGE %s AUDIO %u VIDEO %u", failure_stage.load(),
                  playback_audio_output.load() ? 1U : 0U, playback_video_output.load() ? 1U : 0U);
}
void flexible_memory_status(char *text, std::size_t capacity) noexcept
{
    const int result = flexible_query_result.load();
    if (result == 0)
        std::snprintf(text, capacity, "FMEM BEFORE OPEN %zu MIB",
                      flexible_bytes_before_open.load() / (1024 * 1024));
    else if (result == -1)
        std::snprintf(text, capacity, "FMEM QUERY UNAVAILABLE");
    else
        std::snprintf(text, capacity, "FMEM QUERY ERR %d", result);
}
void memory_allocation_origin_status(unsigned row, char *text, std::size_t capacity) noexcept
{
    const bool ffmpeg = row != 0;
    const unsigned state = (ffmpeg ? ffmpeg_origin_state : first_origin_state).load();
    if (state != 2)
    {
        std::snprintf(text, capacity, "%s NONE", ffmpeg ? "FFALLOC" : "FIRSTFAIL");
        return;
    }
    const AllocationOrigin &origin = ffmpeg ? ffmpeg_origin : first_origin;
    constexpr const char *names[] = {"NONE", "MALLOC", "CALLOC", "REALLOC", "ALIGN", "MALLOCZ"};
    const char *name = origin.kind < 6 ? names[origin.kind] : "UNKNOWN";
    const auto magnitude = origin.relative < 0
                               ? std::uintptr_t{0} - static_cast<std::uintptr_t>(origin.relative)
                               : static_cast<std::uintptr_t>(origin.relative);
    if (origin.kind == 6)
    {
        // For av_fast_realloc the outer caller identifies the actual buffer.
        // Its incoming capacity is saved before FFmpeg clears it on failure.
        std::snprintf(text, capacity, "%s FASTREALLOC NEED %zu OLD %zu REL %s0X%zX",
                      ffmpeg ? "FFALLOC" : "FIRSTFAIL", origin.bytes, origin.alignment,
                      origin.relative < 0 ? "-" : "", static_cast<std::size_t>(magnitude));
        return;
    }
    // The bitmap UI supports uppercase and '-', but neither '+' nor lowercase.
    std::snprintf(text, capacity, "%s %s %zuB A%zu REL %s0X%zX", ffmpeg ? "FFALLOC" : "FIRSTFAIL",
                  name, origin.bytes, origin.alignment, origin.relative < 0 ? "-" : "",
                  static_cast<std::size_t>(magnitude));
}
} // namespace tube5
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
#ifdef TUBE5_GPU_PLAYER
#define __real_malloc tube5_gpu_malloc
#define __real_calloc tube5_gpu_calloc
#define __real_realloc tube5_gpu_realloc
#define __real_posix_memalign tube5_gpu_posix_memalign
#endif
extern "C" int __wrap_posix_memalign(void **, std::size_t, std::size_t);
namespace
{
void record_origin(bool ffmpeg, unsigned allocation, std::size_t bytes, std::size_t alignment,
                   void *caller) noexcept
{
    auto &state = ffmpeg ? ffmpeg_origin_state : first_origin_state;
    unsigned expected = 0;
    if (!state.compare_exchange_strong(expected, 1))
        return;
    const auto address = reinterpret_cast<std::uintptr_t>(caller);
    const auto anchor = reinterpret_cast<std::uintptr_t>(&__wrap_posix_memalign);
    const auto relative = address >= anchor ? static_cast<std::intptr_t>(address - anchor)
                                            : -static_cast<std::intptr_t>(anchor - address);
    (ffmpeg ? ffmpeg_origin : first_origin) = {allocation, bytes, alignment, relative};
    state = 2;
}
void record(unsigned allocation, std::size_t bytes, int result, std::size_t alignment,
            void *caller) noexcept
{
    // Only the aligned API supplies a reliable error code in this diagnostic.
    // Do not confuse EINVAL (bad alignment) with allocator exhaustion.
    if (result == ENOMEM)
        out_of_memory = true;
    failure_stage = current_stage.load();
    ++failures;
    kind = allocation;
    failed_bytes = bytes;
    error = result;
    record_origin(false, allocation, bytes, alignment, caller);
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
        record(1, bytes, 0, 0, __builtin_return_address(0));
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
               0, 0, __builtin_return_address(0));
    return result;
}
extern "C" void *__wrap_realloc(void *address, std::size_t bytes)
{
    void *result = nullptr;
#ifdef TUBE5_PARSER_BUFFER_WRAP
    if (!tube5::reallocate_owned_parser_buffer(address, bytes, result))
#endif
        result = __real_realloc(address, bytes);
    if (!result && bytes)
        record(3, bytes, 0, 0, __builtin_return_address(0));
    return result;
}
extern "C" int __wrap_posix_memalign(void **address, std::size_t alignment, std::size_t bytes)
{
#ifdef TUBE5_PARSER_BUFFER_WRAP
    if (alignment == 64 && tube5::parser_aligned_caller(__builtin_return_address(0)))
        if (void *native = tube5::allocate_parser_buffer(bytes, false))
        {
            *address = native;
            return 0;
        }
#endif
    const int result = __real_posix_memalign(address, alignment, bytes);
    if (result != 0 && bytes)
        record(4, bytes, result, alignment, __builtin_return_address(0));
    return result;
}
#ifdef TUBE5_ALLOC_CALLER_WRAP
// The libc caller often identifies av_malloc only. These outer wrappers expose
// its requestor without stack walking, TLS, logging or allocation in the hook.
extern "C" void *__real_av_malloc(std::size_t);
extern "C" void *__real_av_mallocz(std::size_t);
extern "C" void *__real_av_fast_realloc(void *, unsigned *, std::size_t);
extern "C" void *__wrap_av_fast_realloc(void *address, unsigned *size, std::size_t needed)
{
    const unsigned previous = *size;
    void *result = nullptr;
#ifdef TUBE5_PARSER_BUFFER_WRAP
    if (!tube5::reallocate_mov_index(address, size, needed, __builtin_return_address(0), result))
#endif
        result = __real_av_fast_realloc(address, size, needed);
    if (!result && needed)
        record_origin(true, 6, needed, previous, __builtin_return_address(0));
    return result;
}
extern "C" void *__wrap_av_mallocz(std::size_t bytes)
{
#ifdef TUBE5_PARSER_BUFFER_WRAP
    if (tube5::parser_zero_caller(__builtin_return_address(0)))
        if (void *native = tube5::allocate_parser_buffer(bytes, true))
            return native;
#endif
    void *result = __real_av_mallocz(bytes);
    if (!result && bytes)
        record_origin(true, 5, bytes, 0, __builtin_return_address(0));
    return result;
}
#endif
#endif

#if defined(TUBE5_REFSTRUCT_BUFFER_WRAP) ||                                                        \
    (defined(TUBE5_ALLOC_CALLER_WRAP) && defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST))
extern "C" void *__real_av_malloc(std::size_t);
extern "C" void *__wrap_av_malloc(std::size_t bytes)
{
    void *caller = __builtin_return_address(0);
#ifdef TUBE5_REFSTRUCT_BUFFER_WRAP
    void *result = tube5::allocate_refstruct_buffer(bytes, caller);
#else
    void *result = __real_av_malloc(bytes);
#endif
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
    if (!result && bytes)
        record_origin(true, 1, bytes, 0, caller);
#endif
    return result;
}
#endif
